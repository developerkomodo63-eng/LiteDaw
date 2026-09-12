#pragma once
#include <JuceHeader.h>

/**
    Encapsula el escaneo e instanciación de plugins VST3.
    El escaneo es manual (botón "Escanear VST3...") en vez de automático
    al abrir la app, para no penalizar el arranque en discos eMMC lentos.
*/
class PluginHost
{
public:
    PluginHost();

    using PluginFoundCallback = std::function<void(const juce::PluginDescription&)>;

    /** Escanea las carpetas VST3 estándar del sistema y llama al callback
        una vez por cada plugin encontrado. No bloquea la UI por mucho
        tiempo: se puede mover a un hilo de fondo más adelante si la
        cantidad de plugins instalados crece. */
    void scanForVST3Plugins(PluginFoundCallback onFound);

    /** Instancia un plugin a partir de su descripción. Devuelve nullptr
        si falla (plugin incompatible, corrupto, etc). */
    std::unique_ptr<juce::AudioPluginInstance> createInstance(
        const juce::PluginDescription& description,
        double sampleRate,
        int blockSize);

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /** Plugins ya escaneados y registrados, para poblar el selector de
        cada canal del mixer sin tener que volver a escanear el disco. */
    juce::Array<juce::PluginDescription> getKnownPlugins() const { return knownPlugins.getTypes(); }

private:
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
};
