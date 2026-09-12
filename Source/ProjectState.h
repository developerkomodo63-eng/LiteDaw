#pragma once
#include <JuceHeader.h>
#include "PlaylistComponent.h"

/**
    Formato de proyecto de LiteDAW: un único XML de texto plano
    (extensión .litedaw). Deliberadamente simple:

    - Guarda pistas, clips (con la ruta al archivo de audio) y el estado
      de gain/mute/solo de cada canal del mixer.
    - NO guarda instancias de plugins VST3 (eso requeriría serializar el
      estado interno de cada plugin, que puede pesar mucho y complicar
      el formato). Después de cargar un proyecto, los plugins hay que
      volver a cargarlos a mano desde "Escanear VST3...".

    Este alcance acotado es intencional: mantiene el archivo de proyecto
    liviano y la carga instantánea, coherente con el objetivo general
    de LiteDAW.
*/
struct ProjectClipData
{
    juce::String name;
    juce::String filePath;
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
    int trackIndex = 0;
};

struct ProjectChannelData
{
    float gain = 1.0f;
    bool muted = false;
    bool solo = false;
};

struct ProjectData
{
    juce::StringArray trackNames;
    juce::Array<ProjectClipData> clips;
    juce::Array<ProjectChannelData> channels;
};

namespace ProjectFile
{
    bool save(const juce::File& file, const ProjectData& data);

    /** Devuelve true y llena outData si el XML es válido. */
    bool load(const juce::File& file, ProjectData& outData);
}
