#include "AudioEngine.h"

AudioEngine::AudioEngine()
{
    formatManager.registerBasicFormats(); // wav, aiff, mp3 (donde el SO lo soporte), etc.
    ensureChannelCount(2); // igual que los 2 canales/pistas por defecto del mixer/playlist
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;

    // Reservar de una vez para no alocar en el hilo de audio (ver
    // declaración de estos miembros en el .h). 8 canales de reserva es de
    // sobra hoy y barato; si algún día el output tiene más, setSize()
    // dentro de getNextAudioBlock crecerá el buffer esa única vez.
    channelScratchBuffer.setSize(juce::jmax(2, 8), samplesPerBlockExpected, false, false, true);

    midiCollector.reset(sampleRate);

    for (auto* clip : loadedClips)
        if (clip->readerSource != nullptr)
            clip->readerSource->prepareToPlay(samplesPerBlockExpected, sampleRate);

    const juce::SpinLock::ScopedLockType lock(pluginLock);
    for (auto* ch : channelStates)
        if (ch->plugin != nullptr)
            ch->plugin->prepareToPlay(sampleRate, samplesPerBlockExpected);
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
{
    // Llamado desde el hilo de MIDI de JUCE, no desde el de audio.
    // addMessageToQueue es thread-safe y no bloquea: es justo lo que
    // hace falta para no meter latencia extra entre que se toca una
    // tecla y que el mensaje quede listo para el próximo bloque de audio.
    midiCollector.addMessageToQueue(message);
}

void AudioEngine::releaseResources()
{
    for (auto* clip : loadedClips)
        if (clip->readerSource != nullptr)
            clip->readerSource->releaseResources();

    const juce::SpinLock::ScopedLockType lock(pluginLock);
    for (auto* ch : channelStates)
        if (ch->plugin != nullptr)
            ch->plugin->releaseResources();
}

bool AudioEngine::anyChannelSoloed() const
{
    for (auto* ch : channelStates)
        if (ch->solo.load())
            return true;
    return false;
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    const int numSamples = info.numSamples;
    const int numOutChannels = info.buffer->getNumChannels();
    const bool isPlaying = playing.load();
    const auto blockStartSample = playheadSample.load();
    const bool soloActive = anyChannelSoloed();

    // Buffer temporal reutilizado (ver comentario en el .h): agrandar acá
    // solo toca memoria si numSamples/numOutChannels superan lo ya
    // reservado en prepareToPlay, que es el caso normal. Construir un
    // juce::AudioBuffer nuevo por canal en cada bloque (como antes) es
    // una alocación de heap en el hilo de audio, y eso es exactamente lo
    // que produce clics/xruns cuando el buffer del dispositivo es chico.
    channelScratchBuffer.setSize(juce::jmax(numOutChannels, channelScratchBuffer.getNumChannels()),
                                  numSamples, false, false, true);
    juce::AudioBuffer<float> channelBuffer(channelScratchBuffer.getArrayOfWritePointers(),
                                            numOutChannels, numSamples);

    // MIDI real entrante (teclado/controlador) para este bloque, ya
    // repartido sample-accurate por MidiMessageCollector. Se procesa
    // siempre, toque o no el transporte: un instrumento debe sonar en
    // vivo aunque no se esté reproduciendo la playlist.
    incomingMidiScratch.clear();
    midiCollector.removeNextBlockOfMessages(incomingMidiScratch, numSamples);

    for (int chIdx = 0; chIdx < channelStates.size(); ++chIdx)
    {
        auto* channelState = channelStates.getUnchecked(chIdx);
        channelBuffer.clear();

        if (isPlaying)
        {
            // Sumar todos los clips de esta pista que caen dentro de este bloque.
            for (auto* clip : loadedClips)
            {
                if (clip->trackIndex != chIdx || clip->readerSource == nullptr)
                    continue;

                auto clipEndSample = clip->startSample + clip->lengthInSamples;
                if (blockStartSample + numSamples <= clip->startSample || blockStartSample >= clipEndSample)
                    continue; // este clip no suena en este bloque

                // Posición dentro del propio archivo (0 = inicio del clip).
                auto positionInClip = blockStartSample - clip->startSample;
                clip->readerSource->setNextReadPosition(juce::jmax((juce::int64) 0, positionInClip));

                juce::AudioSourceChannelInfo clipInfo(&channelBuffer, 0, numSamples);
                clip->readerSource->getNextAudioBlock(clipInfo);
            }
        }

        // Cada canal recibe su propia copia del MIDI del bloque: el
        // plugin puede modificar/consumir el buffer que se le pasa y no
        // debería afectar lo que reciben los demás canales.
        channelMidiScratch = incomingMidiScratch;

        // Procesar por el plugin del canal, si tiene uno cargado.
        {
            const juce::SpinLock::ScopedLockType lock(pluginLock);
            if (channelState->plugin != nullptr)
                channelState->plugin->processBlock(channelBuffer, channelMidiScratch);
        }

        // Nivel RMS real para el meter.
        channelState->level.store(channelBuffer.getRMSLevel(0, 0, numSamples));

        const bool audible = !channelState->muted.load() && (!soloActive || channelState->solo.load());
        const float gain = channelState->gain.load();

        if (audible)
            for (int c = 0; c < numOutChannels; ++c)
                info.buffer->addFrom(c, info.startSample, channelBuffer,
                                     juce::jmin(c, channelBuffer.getNumChannels() - 1),
                                     0, numSamples, gain);
    }

    if (isPlaying)
        playheadSample.store(blockStartSample + numSamples);
}

void AudioEngine::play()  { playing.store(true); }
void AudioEngine::stop()  { playing.store(false); }

double AudioEngine::getPlayheadSeconds() const noexcept
{
    return (double) playheadSample.load() / currentSampleRate;
}

void AudioEngine::setPlayheadSeconds(double seconds)
{
    playheadSample.store((juce::int64) (seconds * currentSampleRate));
}

void AudioEngine::setClips(const juce::Array<PlaylistClip>& clips)
{
    // Se asume que esto corre en el hilo de mensajes (ej. al soltar un
    // archivo o agregar un clip), nunca desde el callback de audio.
    juce::OwnedArray<LoadedClip> newClips;

    for (auto& clip : clips)
    {
        if (!clip.audioFile.existsAsFile())
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(clip.audioFile));
        if (reader == nullptr)
            continue;

        auto* loaded = new LoadedClip();
        loaded->readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
        loaded->startSample = (juce::int64) (clip.startSeconds * currentSampleRate);
        loaded->lengthInSamples = (juce::int64) (clip.lengthSeconds * currentSampleRate);
        loaded->trackIndex = clip.trackIndex;
        loaded->readerSource->prepareToPlay(currentBlockSize, currentSampleRate);

        newClips.add(loaded);
    }

    loadedClips.swapWith(newClips); // corto: reemplaza todo de una
}

void AudioEngine::ensureChannelCount(int numChannels)
{
    while (channelStates.size() < numChannels)
        channelStates.add(new ChannelState());
}

void AudioEngine::setChannelPlugin(int channelIndex, juce::AudioPluginInstance* plugin)
{
    ensureChannelCount(channelIndex + 1);
    const juce::SpinLock::ScopedLockType lock(pluginLock);
    channelStates.getUnchecked(channelIndex)->plugin = plugin;
}

void AudioEngine::setChannelGain(int channelIndex, float linearGain)
{
    ensureChannelCount(channelIndex + 1);
    channelStates.getUnchecked(channelIndex)->gain.store(linearGain);
}

void AudioEngine::setChannelMute(int channelIndex, bool shouldMute)
{
    ensureChannelCount(channelIndex + 1);
    channelStates.getUnchecked(channelIndex)->muted.store(shouldMute);
}

void AudioEngine::setChannelSolo(int channelIndex, bool shouldSolo)
{
    ensureChannelCount(channelIndex + 1);
    channelStates.getUnchecked(channelIndex)->solo.store(shouldSolo);
}

float AudioEngine::getChannelLevel(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= channelStates.size())
        return 0.0f;
    return channelStates.getUnchecked(channelIndex)->level.load();
}

void AudioEngine::resetChannels()
{
    jassert(!playing.load()); // debe detenerse el transporte antes de resetear
    channelStates.clear();
    loadedClips.clear();
}
