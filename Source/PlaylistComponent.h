#pragma once
#include <JuceHeader.h>

/** Un clip de audio dibujado como bloque simple en la timeline. */
struct PlaylistClip
{
    juce::String name;
    double startSeconds = 0.0;
    double lengthSeconds = 4.0;
    int trackIndex = 0;
    juce::File audioFile; // vacío = clip "de ejemplo" sin audio real detrás
};

/**
    Timeline horizontal con pistas apiladas. Dibuja los clips como
    rectángulos con nombre — nada de waveforms renderizadas en vivo,
    que es lo que suele comerse CPU en discos/DAWs livianos.
*/
class PlaylistComponent : public juce::Component
{
public:
    PlaylistComponent();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void addTrack(const juce::String& trackName);
    void addClip(const PlaylistClip& clip);

    /** El playhead ahora lo maneja AudioEngine; este componente solo
        dibuja la posición que le pasan (fuente de verdad = motor). */
    void setPlayheadSeconds(double seconds);
    void setPlaying(bool shouldBePlaying);

    const juce::Array<PlaylistClip>& getClips() const { return clips; }
    juce::StringArray getTrackNames() const { return trackNames; }

    /** Borra todas las pistas y clips (usado al cargar un proyecto). */
    void clearAll();

    /** Se llama cada vez que la lista de clips cambia (nuevo clip
        cargado), para que MainComponent le pase el contenido actualizado
        al AudioEngine. */
    std::function<void()> onClipsChanged;

    int getNumTracks() const { return trackNames.size(); }

private:
    static constexpr int trackHeight = 60;
    static constexpr double pixelsPerSecond = 40.0;

    juce::StringArray trackNames;
    juce::Array<PlaylistClip> clips;

    double playheadSeconds = 0.0;
    bool isPlaying = false;

    juce::Rectangle<int> getClipBounds(const PlaylistClip& clip) const;
    int getTrackIndexForY(int y) const;

    std::unique_ptr<juce::FileChooser> fileChooser; // debe vivir durante el diálogo async

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistComponent)
};
