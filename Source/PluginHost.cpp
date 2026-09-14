#include "PluginHost.h"

PluginHost::PluginHost()
{
    // AudioPluginFormatManager::addDefaultFormats() ya no existe (JUCE la
    // marcó "=delete" al partir el hosting de plugins en una variante con
    // soporte de UI y otra "headless" sin editor de plugin). Como el plan
    // es eventualmente abrir el editor nativo del VST3 (ver limitación en
    // el README), usamos la función libre con soporte de UI en vez de la
    // headless (que dejaría hasEditor()==false siempre).
    juce::addDefaultFormatsToManager(formatManager); // registra VST3 (y AU en mac, etc.)
}

void PluginHost::scanForVST3Plugins(PluginFoundCallback onFound, PluginSkippedCallback onSkipped)
{
    auto stateFile = getScanStateFile();
    stateFile.getParentDirectory().createDirectory();

    // Si este archivo YA existía antes de arrancar este escaneo, significa
    // que la vez anterior el proceso entero se cortó justo mientras
    // estábamos leyendo la descripción de ese plugin puntual. Pasa a la
    // lista negra persistente para no volver a intentarlo (y así no
    // repetir el mismo crash cada vez que se escanea).
    if (stateFile.existsAsFile())
    {
        const auto crashedFile = stateFile.loadFileAsString().trim();
        if (crashedFile.isNotEmpty())
        {
            appendToCrashedPluginsList(crashedFile);
            if (onSkipped)
                onSkipped(crashedFile);
        }
    }

    const auto blacklist = loadCrashedPluginsList();

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);

        if (format->getName() != "VST3")
            continue;

        juce::FileSearchPath searchPath(format->getDefaultLocationsToSearch());
        auto filesFound = format->searchPathsForPlugins(searchPath, true, false);

        for (auto& file : filesFound)
        {
            const auto fullPath = file.getFullPathName();

            if (blacklist.contains(fullPath))
            {
                if (onSkipped)
                    onSkipped(fullPath);
                continue;
            }

            // Dejamos "escrito en piedra" (en disco, no en memoria) que
            // estamos por meternos en este plugin ANTES de tocarlo. Si
            // findAllTypesForFile cuelga o crashea el proceso entero, este
            // archivo sigue en disco la próxima vez que se abra la app, y
            // así sabemos exactamente cuál fue el culpable sin adivinar.
            stateFile.replaceWithText(fullPath);

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, file);

            for (auto* desc : found)
            {
                knownPlugins.addType(*desc);
                if (onFound)
                    onFound(*desc);
            }
        }
    }

    // Terminó todo sin cortarse: borramos el rastro para que la próxima
    // vez no se confunda pensando que el último plugin escaneado crasheó.
    stateFile.deleteFile();
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance(
    const juce::PluginDescription& description,
    double sampleRate,
    int blockSize)
{
    juce::String errorMessage;
    auto instance = formatManager.createPluginInstance(
        description, sampleRate, blockSize, errorMessage);

    if (instance == nullptr)
        juce::Logger::writeToLog("Error cargando plugin: " + errorMessage);

    return instance;
}

// ------------------------------------------------- Estado de escaneo persistente

juce::File PluginHost::getScanStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LiteDAW")
        .getChildFile("escaneando_ahora.txt");
}

juce::File PluginHost::getCrashedPluginsFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LiteDAW")
        .getChildFile("plugins_bloqueados.txt");
}

juce::StringArray PluginHost::loadCrashedPluginsList() const
{
    auto file = getCrashedPluginsFile();
    if (!file.existsAsFile())
        return {};

    juce::StringArray lines;
    file.readLines(lines);
    lines.removeEmptyStrings();
    return lines;
}

void PluginHost::appendToCrashedPluginsList(const juce::String& filePath) const
{
    auto file = getCrashedPluginsFile();
    file.getParentDirectory().createDirectory();
    file.appendText(filePath + "\n");
}

void PluginHost::clearCrashedPluginsList() const
{
    getCrashedPluginsFile().deleteFile();
}
