#pragma once
#include <JuceHeader.h>
#include "PluginHost.h"
#include "AudioEngine.h"

/** Un canal individual del mixer: fader, mute, solo y una cadena de
    plugins VST3 (cero, uno o varios en serie). Todas las acciones del
    usuario (mover fader, mute, solo, agregar/quitar/reordenar plugins)
    se reflejan de inmediato en el AudioEngine real. */
class MixerChannel : public juce::Component
{
public:
    MixerChannel(juce::String name, int channelIndex, PluginHost& host, AudioEngine& engine);

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Agrega un plugin al final de la cadena de este canal (no reemplaza
        los que ya estén cargados). Se instancia con el sample rate/block
        size REALES del dispositivo en uso, igual que antes. */
    void addPluginToChain(const juce::PluginDescription& description);
    void refreshMeter();

    float getGain() const { return (float) volumeFader.getValue(); }
    bool getMuted() const { return muteButton.getToggleState(); }
    bool getSolo() const { return soloButton.getToggleState(); }

    /** Aplica un estado guardado (gain/mute/solo) a la UI y al AudioEngine
        de una sola vez, sin pasar por los callbacks de click del usuario. */
    void applyState(float gain, bool muted, bool solo);

private:
    /** Ventana nativa para la GUI propia de un plugin. No es dueña de la
        instancia del plugin -sólo aloja su AudioProcessorEditor mientras
        la ventana está abierta-: cerrarla no descarga el plugin, el
        plugin sigue procesando audio en vivo igual. Se crea recién al
        tocar "Ver GUI", nunca antes de eso: instanciar el editor nativo
        de un plugin puede ser pesado, y no tiene sentido pagar ese costo
        para plugins cuya GUI nunca se llega a abrir (ver limitación en
        el README sobre esto). */
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        PluginEditorWindow(juce::AudioPluginInstance& pluginToShow, std::function<void()> onCloseCallback);
        void closeButtonPressed() override;

        juce::AudioPluginInstance* plugin; // sin ownership, para ubicar la ventana de un plugin dado

    private:
        std::function<void()> onClose;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
    };

    void showPluginChainMenu();
    void removePluginFromChain(int index);
    void movePluginInChain(int index, int delta);
    void openPluginEditor(int index);
    void togglePluginBypass(int index);
    void syncChainToEngine();
    void updatePluginButtonText();
    void showInputMenu();

    PluginHost& pluginHost;
    AudioEngine& audioEngine;
    int channelIndex;

    juce::String channelName;
    juce::Slider volumeFader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    juce::TextButton pluginSlotButton { "Plugins (0)" };
    juce::TextButton inputSlotButton { "In: -" };
    juce::Label nameLabel;

    /** Un plugin cargado en la cadena de este canal, con su estado de
        bypass ("off"). El canal es el dueño real de la instancia; el
        AudioEngine solo recibe punteros crudos + el flag de bypass (ver
        AudioEngine::PluginSlot / syncChainToEngine). Usar un solo array
        de estos en vez de dos arrays paralelos (instancias y bypass por
        separado) evita que un reordenamiento (Subir/Bajar) desincronice
        cuál bypass le corresponde a cuál plugin. */
    struct LoadedPlugin
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        bool bypassed = false;
    };

    // Declarado ANTES de openEditorWindows a propósito: los miembros se
    // destruyen en orden inverso de declaración, así que las ventanas de
    // GUI (que referencian un plugin) se cierran antes de que el plugin
    // mismo se destruya.
    juce::OwnedArray<LoadedPlugin> pluginChain;
    juce::OwnedArray<PluginEditorWindow> openEditorWindows;

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
