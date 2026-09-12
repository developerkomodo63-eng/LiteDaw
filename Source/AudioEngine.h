#pragma once
#include <JuceHeader.h>
#include "PlaylistComponent.h"

/**
    Motor de audio real de LiteDAW.

    Es un único juce::AudioSource que:
    - Mezcla los clips de audio cargados (leídos desde disco) respetando
      su posición en el tiempo.
    - Pasa la señal de cada canal por su plugin VST3 (si tiene uno cargado).
    - Aplica gain/mute/solo por canal.
    - Calcula un nivel RMS real por canal para los meters del mixer.

    Simplificaciones deliberadas (documentadas para no confundirlas con
    bugs): el puntero al plugin se protege con un juce::SpinLock en vez
    de un diseño lock-free más elaborado, y los canales "extra" creados
    al escanear plugins (sin pista de playlist asociada) no reciben
    clips — son slots de instrumento/efecto para uso futuro.
*/
class AudioEngine : public juce::AudioSource
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // --- juce::AudioSource ------------------------------------------------
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

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
    void setChannelPlugin(int channelIndex, juce::AudioPluginInstance* plugin);
    void setChannelGain(int channelIndex, float linearGain);
    void setChannelMute(int channelIndex, bool shouldMute);
    void setChannelSolo(int channelIndex, bool shouldSolo);
    float getChannelLevel(int channelIndex) const;

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
        juce::AudioPluginInstance* plugin = nullptr; // no ownership
    };

    juce::AudioFormatManager formatManager;
    juce::OwnedArray<LoadedClip> loadedClips;
    juce::OwnedArray<ChannelState> channelStates;

    juce::SpinLock pluginLock;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 1024;

    std::atomic<juce::int64> playheadSample { 0 };
    std::atomic<bool> playing { false };

    bool anyChannelSoloed() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
