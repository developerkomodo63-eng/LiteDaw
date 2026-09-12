#include "MainComponent.h"

MainComponent::MainComponent()
    : mixer(pluginHost, audioEngine)
{
    // Buffer grande + sample rate moderado: prioriza estabilidad sobre
    // latencia ultra baja, ideal para reproducir en una presentación
    // sin xruns en un CPU limitado.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.initialiseWithDefaultDevices(0, 2);
    deviceManager.getAudioDeviceSetup(setup);
    setup.bufferSize = 1024;
    deviceManager.setAudioDeviceSetup(setup, true);

    // Conecta el motor real al hardware de audio.
    audioSourcePlayer.setSource(&audioEngine);
    deviceManager.addAudioCallback(&audioSourcePlayer);

    addAndMakeVisible(playlist);
    playlist.onClipsChanged = [this] { audioEngine.setClips(playlist.getClips()); };

    addAndMakeVisible(mixer);

    addAndMakeVisible(scanPluginsButton);
    scanPluginsButton.onClick = [this] { scanForPlugins(); };

    addAndMakeVisible(addTrackButton);
    addTrackButton.onClick = [this] { addTrack(); };

    addAndMakeVisible(playButton);
    playButton.onClick = [this]
    {
        audioEngine.play();
        playlist.setPlaying(true);
    };

    addAndMakeVisible(stopButton);
    stopButton.onClick = [this]
    {
        audioEngine.stop();
        playlist.setPlaying(false);
    };

    addAndMakeVisible(saveButton);
    saveButton.onClick = [this] { saveProject(); };

    addAndMakeVisible(openButton);
    openButton.onClick = [this] { loadProject(); };

    setSize(1100, 650);

    // 15 fps para meters y playhead: de sobra visualmente, barato en CPU.
    startTimerHz(15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioSourcePlayer.setSource(nullptr);
    deviceManager.removeAudioCallback(&audioSourcePlayer);
    deviceManager.closeAudioDevice();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto toolbar = area.removeFromTop(36).reduced(4);
    scanPluginsButton.setBounds(toolbar.removeFromLeft(140));
    toolbar.removeFromLeft(6);
    addTrackButton.setBounds(toolbar.removeFromLeft(90));
    toolbar.removeFromLeft(6);
    playButton.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(4);
    stopButton.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(12);
    saveButton.setBounds(toolbar.removeFromLeft(80));
    toolbar.removeFromLeft(4);
    openButton.setBounds(toolbar.removeFromLeft(80));

    playlist.setBounds(area.removeFromTop(area.getHeight() * 6 / 10));
    mixer.setBounds(area);
}

void MainComponent::timerCallback()
{
    playlist.setPlayheadSeconds(audioEngine.getPlayheadSeconds());
    mixer.refreshMeters();
}

void MainComponent::scanForPlugins()
{
    // Solo puebla la lista de PluginHost — ya NO crea un canal por cada
    // plugin encontrado (con muchos plugins instalados eso volvía la app
    // pesada de arrancar/usar, justo lo opuesto a "ultra liviano").
    // Cada canal elige su plugin con un click en su propio slot.
    int foundCount = 0;
    pluginHost.scanForVST3Plugins([&foundCount](const juce::PluginDescription&) { ++foundCount; });

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "Escaneo de plugins",
        foundCount > 0
            ? "Se encontraron " + juce::String(foundCount)
                  + " plugin(s) VST3. Click en el slot de un canal para asignarle uno."
            : "No se encontraron plugins VST3 instalados en las rutas por defecto.");
}

void MainComponent::addTrackNamed(const juce::String& name)
{
    playlist.addTrack(name);
    mixer.addTrackChannel(name.replace("Pista", "Canal"));
}

void MainComponent::addTrack()
{
    const int trackNumber = playlist.getNumTracks() + 1;
    addTrackNamed("Pista " + juce::String(trackNumber) + " - Audio");
}

void MainComponent::saveProject()
{
    ProjectData data;
    data.trackNames = playlist.getTrackNames();

    for (auto& clip : playlist.getClips())
    {
        ProjectClipData clipData;
        clipData.name = clip.name;
        clipData.filePath = clip.audioFile.getFullPathName();
        clipData.startSeconds = clip.startSeconds;
        clipData.lengthSeconds = clip.lengthSeconds;
        clipData.trackIndex = clip.trackIndex;
        data.clips.add(clipData);
    }

    for (int i = 0; i < mixer.getChannelCount(); ++i)
    {
        ProjectChannelData channelData;
        mixer.getChannelState(i, channelData.gain, channelData.muted, channelData.solo);
        data.channels.add(channelData);
    }

    fileChooser = std::make_unique<juce::FileChooser>(
        "Guardar proyecto como...", juce::File(), "*.litedaw");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [data](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            if (!file.hasFileExtension(".litedaw"))
                file = file.withFileExtension(".litedaw");

            ProjectFile::save(file, data);
        });
}

void MainComponent::loadProject()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Abrir proyecto...", juce::File(), "*.litedaw");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile())
                return;

            ProjectData data;
            if (!ProjectFile::load(file, data))
                return;

            // Parar el transporte antes de reconstruir todo: resetChannels()
            // no es seguro de llamar mientras el motor está reproduciendo.
            audioEngine.stop();
            playlist.setPlaying(false);

            playlist.clearAll();
            mixer.clearAll();
            audioEngine.resetChannels();

            for (auto& name : data.trackNames)
                addTrackNamed(name);

            for (auto& clipData : data.clips)
            {
                PlaylistClip clip;
                clip.name = clipData.name;
                clip.audioFile = juce::File(clipData.filePath);
                clip.startSeconds = clipData.startSeconds;
                clip.lengthSeconds = clipData.lengthSeconds;
                clip.trackIndex = clipData.trackIndex;
                playlist.addClip(clip);
            }

            for (int i = 0; i < data.channels.size(); ++i)
                mixer.applyChannelState(i, data.channels[i].gain,
                                         data.channels[i].muted, data.channels[i].solo);
        });
}
