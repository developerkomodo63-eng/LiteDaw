#include "PlaylistComponent.h"

PlaylistComponent::PlaylistComponent()
{
    // Dos pistas de ejemplo, vacías (sin audio real todavía).
    // Doble click sobre una pista para cargarle un archivo de audio.
    addTrack("Pista 1 - Audio");
    addTrack("Pista 2 - Audio");
}

void PlaylistComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff141414));

    for (int i = 0; i < trackNames.size(); ++i)
    {
        auto rowTop = i * trackHeight;
        g.setColour(juce::Colour(0xff2b2b2b));
        g.drawLine(0.0f, (float) rowTop, (float) getWidth(), (float) rowTop);

        g.setColour(juce::Colours::lightgrey);
        g.drawText(trackNames[i], 4, rowTop + 2, 200, 16, juce::Justification::left);
    }

    if (clips.isEmpty())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Doble click en una pista para cargar un audio...",
                   10, getHeight() / 2 - 8, 400, 16, juce::Justification::left);
    }

    for (auto& clip : clips)
    {
        auto bounds = getClipBounds(clip);
        auto colour = clip.audioFile.existsAsFile() ? juce::Colour(0xff4a7ea8)
                                                     : juce::Colour(0xff707070);
        g.setColour(colour);
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.drawText(clip.name, bounds.reduced(4), juce::Justification::centredLeft);
    }

    if (isPlaying)
    {
        int x = (int) (playheadSeconds * pixelsPerSecond);
        g.setColour(juce::Colours::orange);
        g.drawLine((float) x, 0.0f, (float) x, (float) getHeight(), 2.0f);
    }
}

void PlaylistComponent::resized() {}

void PlaylistComponent::mouseDown(const juce::MouseEvent& event)
{
    playheadSeconds = juce::jmax(0.0, event.position.x / pixelsPerSecond);
    repaint();
}

void PlaylistComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const int trackIndex = getTrackIndexForY((int) event.position.y);
    if (trackIndex < 0)
        return;

    const double clickSeconds = juce::jmax(0.0, event.position.x / pixelsPerSecond);

    fileChooser = std::make_unique<juce::FileChooser>(
        "Elegí un archivo de audio...",
        juce::File(),
        "*.wav;*.aif;*.aiff;*.mp3;*.flac");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, trackIndex, clickSeconds](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            // Duración real del archivo vía un lector rápido, así el
            // bloque en la timeline representa el largo verdadero.
            juce::AudioFormatManager tempManager;
            tempManager.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(tempManager.createReaderFor(file));

            double lengthSeconds = 4.0;
            if (reader != nullptr && reader->sampleRate > 0.0)
                lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;

            PlaylistClip clip;
            clip.name = file.getFileNameWithoutExtension();
            clip.startSeconds = clickSeconds;
            clip.lengthSeconds = lengthSeconds;
            clip.trackIndex = trackIndex;
            clip.audioFile = file;

            addClip(clip);
        });
}

void PlaylistComponent::addTrack(const juce::String& trackName)
{
    trackNames.add(trackName);
    setSize(getWidth(), trackNames.size() * trackHeight);
}

void PlaylistComponent::addClip(const PlaylistClip& clip)
{
    clips.add(clip);
    repaint();

    if (onClipsChanged)
        onClipsChanged();
}

void PlaylistComponent::setPlayheadSeconds(double seconds)
{
    playheadSeconds = seconds;
    repaint();
}

void PlaylistComponent::setPlaying(bool shouldBePlaying)
{
    isPlaying = shouldBePlaying;
    repaint();
}

juce::Rectangle<int> PlaylistComponent::getClipBounds(const PlaylistClip& clip) const
{
    int x = (int) (clip.startSeconds * pixelsPerSecond);
    int w = juce::jmax(20, (int) (clip.lengthSeconds * pixelsPerSecond));
    int y = clip.trackIndex * trackHeight + 20;
    int h = trackHeight - 24;
    return { x, y, w, h };
}

int PlaylistComponent::getTrackIndexForY(int y) const
{
    const int index = y / trackHeight;
    return (index >= 0 && index < trackNames.size()) ? index : -1;
}

void PlaylistComponent::clearAll()
{
    trackNames.clear();
    clips.clear();
    setSize(getWidth(), 0);
    repaint();

    if (onClipsChanged)
        onClipsChanged();
}
