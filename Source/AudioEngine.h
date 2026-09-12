#pragma once
#include <JuceHeader.h>
#include "PlaylistComponent.h"

/**
    Motor de audio real de LiteDAW.

    Es un juce::AudioIODeviceCallback (no un juce::AudioSource) porque
    necesita acceso directo a la entrada de la interfaz de audio, no solo
    a la salida: un juce::AudioSourcePlayer solo expone el buffer de
    salida al AudioSource, así que para meter la entrada en vivo al
    mixer no queda otra que hablar directo con el dispositivo.

    Por canal:
    - Mezcla los clips de audio cargados (leídos desde disco) respetando
      su posición en el tiempo (si el transporte está reproduciendo).
    - Suma la entrada en vivo de la interfaz de audio, si el canal tiene
      una asignada (setChannelInput) — así se puede meter una guitarra o
      un micrófono de la interfaz a un canal del mixer.
    - Pasa la señal resultante por la cadena de plugins VST3 del canal
      (cero, uno o varios, en serie, en el orden en que fueron agregados),
      junto con el MIDI en vivo del bloque.
    - Aplica gain/mute/solo por canal.
    - Calcula un nivel RMS real por canal para los meters del mixer.

    Simplificaciones deliberadas (documentadas para no confundirlas con
    bugs): la cadena de plugins se protege con un juce::SpinLock en vez
    de un diseño lock-free más elaborado, y los canales "extra" creados
    al escanear plugins (sin pista de playlist asociada) no reciben
    clips — son slots de instrumento/efecto para uso futuro. Tampoco hay
    compensación de latencia entre canales por el delay que puede
    introducir un plugin (getLatencySamples()): con cadenas cortas y
    plugins pensados para tocar en vivo esto no suele notarse, pero un
    plugin con mucho lookahead sí podría desalinearse contra otros
    canales — ver limitaciones en el README.
*/
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // --- juce::AudioIODeviceCallback -----------------------------------
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // --- juce::MidiInputCallback --------------------------------------
    /** Se registra una vez por dispositivo MIDI (habilitado o no) en
        MainComponent. Solo encola el mensaje con su timestamp real; el
        trabajo pesado (repartirlo en el bloque correcto) lo hace
        MidiMessageCollector desde el hilo de audio. Mantener esto liviano
        es lo que permite baja latencia con teclados MIDI. */
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    int getCurrentBlockSize() const noexcept { return currentBlockSize; }

    // --- Transporte ---------------------------------------------------
    void play();
    void stop();
    bool isPlaying() const noexcept { return playing.load(); }

    double getPlayheadSeconds() const noexcept;
    void setPlayheadSeconds(double seconds);

    // --- Contenido ------------------------------------------------------
    /** Reconstruye la lista interna de clips a partir de los de la playlist.
        Abre los archivos de audio en el hilo de mensajes (no en el de audio). */
    void setClips(const juce::Array<PlaylistClip>& clips);

    // --- Canales ----------------------------------------------------------
    void ensureChannelCount(int numChannels);

    /** Un eslabón de la cadena de plugins de un canal. Sin ownership sobre
        la instancia -el canal del mixer sigue siendo el dueño real y
        decide cuándo destruirla-. "bypassed" deja el plugin cargado y con
        su estado interno intacto, pero saltea su processBlock en cada
        bloque de audio: es el equivalente a un botón de encendido/apagado
        por plugin, sin tener que quitarlo de la cadena. */
    struct PluginSlot
    {
        juce::AudioPluginInstance* plugin = nullptr;
        bool bypassed = false;
    };

    /** Reemplaza toda la cadena de plugins de un canal, de una sola vez,
        en el orden en que deben procesarse (el primero de la lista recibe
        la señal primero). Reemplazar la cadena completa (en vez de
        exponer add/remove/mover/bypass acá) evita tener que sincronizar
        múltiples llamadas bajo lock con la UI. */
    void setChannelPluginChain(int channelIndex, const juce::Array<PluginSlot>& chain);
    void setChannelGain(int channelIndex, float linearGain);
    void setChannelMute(int channelIndex, bool shouldMute);
    void setChannelSolo(int channelIndex, bool shouldSolo);
    float getChannelLevel(int channelIndex) const;

    /** Asigna qué canal de entrada de la interfaz de audio alimenta a
        este canal del mixer en vivo. -1 = ninguno (solo clips/plugin). */
    void setChannelInput(int channelIndex, int hardwareInputChannel);
    int getChannelInput(int channelIndex) const;

    /** Cantidad de entradas de audio activas en la interfaz actual (según
        el último audioDeviceAboutToStart). Para poblar el selector de
        entrada de cada canal en la UI. */
    int getNumHardwareInputChannels() const noexcept { return numHardwareInputChannels; }

    /** Borra canales y clips cargados. Llamar solo con isPlaying() en
        false (ej. antes de cargar un proyecto nuevo). */
    void resetChannels();

private:
    struct LoadedClip
    {
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        juce::int64 startSample = 0;
        juce::int64 lengthInSamples = 0;
        int trackIndex = 0;
    };

    struct ChannelState
    {
        std::atomic<float> gain { 1.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
        std::atomic<float> level { 0.0f };
        std::atomic<int> inputChannel { -1 }; // -1 = sin entrada en vivo asignada

        // Cadena de plugins del canal, en orden de procesamiento;
        // protegida por pluginLock, igual que antes cuando era un único
        // puntero.
        juce::Array<PluginSlot> pluginChain;
    };

    juce::AudioFormatManager formatManager;
    juce::OwnedArray<LoadedClip> loadedClips;
    juce::OwnedArray<ChannelState> channelStates;

    juce::SpinLock pluginLock;

    // Recolecta mensajes MIDI entrantes (de cualquier teclado/controlador
    // habilitado) con timestamp real y los reparte sample-accurate dentro
    // del bloque de audio actual. Es la pieza que permite tocar en vivo
    // con latencia baja: sin esto, los plugins de instrumento nunca
    // reciben notas.
    juce::MidiMessageCollector midiCollector;

    // Buffers de trabajo pre-alocados en audioDeviceAboutToStart: reservar
    // memoria dentro del callback de audio (hilo de tiempo real) puede
    // causar clics/xruns, y eso se nota mucho más cuanto más bajo es el
    // buffer del dispositivo. setSize(..., avoidReallocating=true) hace
    // que estas llamadas dentro del callback sean gratis mientras el
    // tamaño pedido entre en lo ya reservado.
    juce::AudioBuffer<float> channelScratchBuffer;
    juce::MidiBuffer incomingMidiScratch;
    juce::MidiBuffer channelMidiScratch;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 1024;
    int numHardwareInputChannels = 0;

    std::atomic<juce::int64> playheadSample { 0 };
    std::atomic<bool> playing { false };

    bool anyChannelSoloed() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
