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
    using PluginSkippedCallback = std::function<void(const juce::String& filePath)>;

    /** Escanea las carpetas VST3 estándar del sistema y llama a onFound
        una vez por cada plugin encontrado. No bloquea la UI por mucho
        tiempo: se puede mover a un hilo de fondo más adelante si la
        cantidad de plugins instalados crece.

        Protección contra plugins rotos: para leer la descripción de un
        VST3 hace falta cargar su binario en este mismo proceso -no hay
        sandbox-, así que un plugin corrupto o incompatible puede crashear
        toda la aplicación a mitad de escaneo (ningún try/catch de C++
        puede atajar eso: un crash nativo no es una excepción de C++). Para
        no quedar reescaneando -y crasheando con- el mismo plugin cada vez,
        se deja un rastro en disco de cuál archivo se está por escanear
        justo antes de tocarlo; si la próxima vez ese rastro sigue ahí
        (o sea, el proceso se cortó ahí mismo), ese archivo pasa a una
        lista negra persistente y se saltea de ahí en más, avisando por
        onSkipped. */
    void scanForVST3Plugins(PluginFoundCallback onFound, PluginSkippedCallback onSkipped = nullptr);

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

    /** Vacía la lista negra de plugins que crashearon un escaneo anterior.
        Útil si un "crash" en realidad fue otra cosa (ej. Windows Update
        cerrando todo) y el plugin en realidad anda bien. */
    void clearCrashedPluginsList() const;

private:
    juce::File getScanStateFile() const;
    juce::File getCrashedPluginsFile() const;
    juce::StringArray loadCrashedPluginsList() const;
    void appendToCrashedPluginsList(const juce::String& filePath) const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
};
