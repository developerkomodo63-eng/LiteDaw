#pragma once
#include <JuceHeader.h>
#include "MixerComponent.h"
#include "PlaylistComponent.h"
#include "PluginHost.h"
#include "AudioEngine.h"
#include "ProjectState.h"

/**
    Ventana principal: arriba la playlist (timeline de pistas/clips),
    abajo el mixer. El audio real corre a través de AudioEngine, que se
    registra directo como juce::AudioIODeviceCallback del dispositivo
    (no vía juce::AudioSourcePlayer) para poder tener acceso a la
    entrada real de la interfaz y así alimentar el input en vivo de
    cada canal del mixer.
*/
class MainComponent : public juce::Component,
                       private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::AudioDeviceManager deviceManager;

    PluginHost pluginHost;
    AudioEngine audioEngine;

    PlaylistComponent playlist;
    MixerComponent mixer;

    juce::TextButton scanPluginsButton { "Escanear VST3..." };
    juce::TextButton addTrackButton    { "+ Pista" };
    juce::TextButton playButton        { "Play" };
    juce::TextButton stopButton        { "Stop" };
    juce::TextButton saveButton        { "Guardar" };
    juce::TextButton openButton        { "Abrir" };
    juce::TextButton audioSettingsButton { "Audio/MIDI..." };

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::DialogWindow> audioSettingsWindow;

    void scanForPlugins();
    void addTrack();
    void addTrackNamed(const juce::String& name);
    void saveProject();
    void loadProject();
    void configureLowLatencyDefaults();
    void enableAllMidiInputs();
    void openAudioSettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
