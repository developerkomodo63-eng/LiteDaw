#include "PluginHost.h"

PluginHost::PluginHost()
{
    formatManager.addDefaultFormats(); // registra VST3 (y AU en mac, etc.)
}

void PluginHost::scanForVST3Plugins(PluginFoundCallback onFound)
{
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);

        if (format->getName() != "VST3")
            continue;

        juce::FileSearchPath searchPath(format->getDefaultLocationsToSearch());
        auto filesFound = format->searchPathsForPlugins(searchPath, true, false);

        for (auto& file : filesFound)
        {
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
