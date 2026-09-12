#pragma once
#include <JuceHeader.h>
#include "PluginHost.h"
#include "AudioEngine.h"

/** Un canal individual del mixer: fader, mute, solo y slot de plugin VST3.
    Todas las acciones del usuario (mover fader, mute, solo, cargar
    plugin) se reflejan de inmediato en el AudioEngine real. */
class MixerChannel : public juce::Component
{
public:
    MixerChannel(juce::String name, int channelIndex, PluginHost& host, AudioEngine& engine);

    void paint(juce::Graphics&) override;
    void resized() override;

    void loadPlugin(const juce::PluginDescription& description);
    void refreshMeter();

    float getGain() const { return (float) volumeFader.getValue(); }
    bool getMuted() const { return muteButton.getToggleState(); }
    bool getSolo() const { return soloButton.getToggleState(); }

    /** Aplica un estado guardado (gain/mute/solo) a la UI y al AudioEngine
        de una sola vez, sin pasar por los callbacks de click del usuario. */
    void applyState(float gain, bool muted, bool solo);

private:
    void showPluginMenu();
    void unloadPlugin();

    PluginHost& pluginHost;
    AudioEngine& audioEngine;
    int channelIndex;

    juce::String channelName;
    juce::Slider volumeFader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::TextButton pluginSlotButton { "(vacío)" };
    juce::Label nameLabel;

    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;

    float currentLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerChannel)
};

/** Fila horizontal de canales, scrolleable si hay muchos. */
class MixerComponent : public juce::Component
{
public:
    MixerComponent(PluginHost& host, AudioEngine& engine);

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Agrega un canal "de pista" (1:1 con una pista de la playlist). */
    void addTrackChannel(const juce::String& name);

    /** Agrega un canal extra con un plugin ya cargado (slot de
        instrumento/efecto sin pista de playlist asociada). */
    void addChannelWithPlugin(const juce::PluginDescription& description);

    void refreshMeters();

    int getChannelCount() const { return channels.size(); }
    void getChannelState(int index, float& gain, bool& muted, bool& solo) const;
    void applyChannelState(int index, float gain, bool muted, bool solo);

    /** Borra todos los canales (usado al cargar un proyecto; llamar solo
        con el motor detenido). */
    void clearAll();

private:
    PluginHost& pluginHost;
    AudioEngine& audioEngine;
    juce::Viewport viewport;
    juce::Component channelHolder;
    juce::OwnedArray<MixerChannel> channels;

    static constexpr int channelWidth = 90;

    void layoutChannels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerComponent)
};
