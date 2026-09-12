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
    conecta al hardware con un juce::AudioSourcePlayer.
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
    juce::AudioSourcePlayer audioSourcePlayer;

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

    std::unique_ptr<juce::FileChooser> fileChooser;

    void scanForPlugins();
    void addTrack();
    void addTrackNamed(const juce::String& name);
    void saveProject();
    void loadProject();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
