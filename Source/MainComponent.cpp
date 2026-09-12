#include "MainComponent.h"

MainComponent::MainComponent()
    : mixer(pluginHost, audioEngine)
{
    // Pedimos entrada de audio (antes era 0 in / 2 out): sin esto la
    // interfaz nunca entrega señal de entrada, sin importar qué tan
    // bajo esté el buffer. Si la interfaz tiene más de 2 entradas, se
    // pueden habilitar el resto desde "Audio/MIDI...".
    deviceManager.initialiseWithDefaultDevices(2, 2);
    configureLowLatencyDefaults();
    enableAllMidiInputs();

    // AudioEngine se registra directo como callback del dispositivo (no
    // vía AudioSourcePlayer): así tiene acceso al buffer de entrada real
    // de la interfaz, necesario para meter esa señal a un canal del mixer.
    deviceManager.addAudioCallback(&audioEngine);

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

    addAndMakeVisible(audioSettingsButton);
    audioSettingsButton.onClick = [this] { openAudioSettings(); };

    setSize(1100, 650);

    // 15 fps para meters y playhead: de sobra visualmente, barato en CPU.
    startTimerHz(15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeAudioCallback(&audioEngine);

    // Sacar los callbacks MIDI antes de que audioEngine se destruya (el
    // orden de destrucción de miembros deja a audioEngine morir antes que
    // deviceManager): si no, un mensaje MIDI de último momento podría
    // llegar a un objeto ya destruido.
    for (auto& midiInput : juce::MidiInput::getAvailableDevices())
        deviceManager.removeMidiInputDeviceCallback(midiInput.identifier, &audioEngine);

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
    toolbar.removeFromLeft(12);
    audioSettingsButton.setBounds(toolbar.removeFromLeft(110));

    playlist.setBounds(area.removeFromTop(area.getHeight() * 6 / 10));
    mixer.setBounds(area);
}

void MainComponent::timerCallback()
{
    playlist.setPlayheadSeconds(audioEngine.getPlayheadSeconds());
    mixer.refreshMeters();
}

void MainComponent::configureLowLatencyDefaults()
{
    // Antes: buffer fijo en 1024 muestras (~23ms @44.1kHz), pensado solo
    // para reproducir pistas ya armadas en una presentación. Eso es
    // demasiada latencia para tocar un teclado MIDI en vivo o monitorear
    // un efecto en tiempo real. Elegimos el buffer más chico que el
    // dispositivo actual soporte, con un piso de seguridad para no caer
    // en un tamaño tan chico que el propio driver no pueda sostenerlo de
    // forma estable.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        auto availableSizes = device->getAvailableBufferSizes();
        int chosen = device->getDefaultBufferSize();

        for (auto size : availableSizes)
            if (size >= 32 && size < chosen)
                chosen = size;

        setup.bufferSize = chosen;
    }
    else
    {
        setup.bufferSize = 256; // ~5.8ms @44.1kHz, razonable si no hay device info
    }

    deviceManager.setAudioDeviceSetup(setup, true);
}

void MainComponent::enableAllMidiInputs()
{
    // La app no tenía NINGÚN manejo de MIDI: un teclado conectado no
    // hacía nada, sin importar la latencia del audio. Habilitamos todos
    // los dispositivos MIDI de entrada disponibles y registramos el
    // AudioEngine como callback de cada uno, para que las notas lleguen
    // en vivo a cualquier plugin de instrumento cargado en un canal.
    for (auto& midiInput : juce::MidiInput::getAvailableDevices())
    {
        if (!deviceManager.isMidiInputDeviceEnabled(midiInput.identifier))
            deviceManager.setMidiInputDeviceEnabled(midiInput.identifier, true);

        deviceManager.addMidiInputDeviceCallback(midiInput.identifier, &audioEngine);
    }
}

void MainComponent::openAudioSettings()
{
    // Selector estándar de JUCE: deja elegir dispositivo, sample rate,
    // tamaño de buffer y qué entradas MIDI están activas, todo desde la
    // app, sin tener que tocar código para bajar más la latencia.
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        0, 16,    // canales de entrada de audio (min/max) — interfaces multicanal
        0, 16,    // canales de salida de audio (min/max)
        true,     // mostrar selector de entradas MIDI
        false,    // mostrar selector de salidas MIDI
        true,     // mostrar canales como pares estéreo
        false);   // no ocultar opciones avanzadas detrás de un botón

    selector->setSize(500, 450);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector);
    options.dialogTitle = "Configuración de Audio / MIDI";
    options.dialogBackgroundColour = juce::Colour(0xff2a2a2a);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    audioSettingsWindow.reset(options.launchAsync());
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
