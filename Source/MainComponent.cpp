#include "MainComponent.h"

MainComponent::MainComponent()
    : mixer(pluginHost, audioEngine)
{
    // Escuchar cambios del dispositivo (tipo, buffer, sample rate) para
    // mantener latencyLabel al día sin sondearlo en cada frame del timer
    // -que sería trabajo desperdiciado la inmensa mayoría del tiempo-.
    deviceManager.addChangeListener(this);

    // Pedimos entrada de audio (antes era 0 in / 2 out): sin esto la
    // interfaz nunca entrega señal de entrada, sin importar qué tan
    // bajo esté el buffer. Si la interfaz tiene más de 2 entradas, se
    // pueden habilitar el resto desde "Audio/MIDI...".
    deviceManager.initialiseWithDefaultDevices(2, 2);

    // Elegir el tipo de dispositivo de menor latencia disponible ANTES de
    // fijar el tamaño de buffer: no tiene sentido optimizar el buffer si
    // seguimos en un driver que de por sí agrega decenas de ms (ej.
    // DirectSound en vez de WASAPI exclusivo, o compilar con soporte ASIO
    // -ver CMakeLists.txt- si el usuario tiene un driver ASIO instalado).
    selectLowestLatencyDeviceType();
    preferAudioInterfaceDevice();
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

    latencyLabel.setJustificationType(juce::Justification::centredRight);
    latencyLabel.setFont(juce::Font(12.0f));
    latencyLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(latencyLabel);
    updateLatencyLabel();

    setSize(1100, 650);

    // 15 fps para meters y playhead: de sobra visualmente, barato en CPU.
    startTimerHz(15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    deviceManager.removeChangeListener(this);
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

    latencyLabel.setBounds(toolbar);

    playlist.setBounds(area.removeFromTop(area.getHeight() * 6 / 10));
    mixer.setBounds(area);
}

void MainComponent::timerCallback()
{
    playlist.setPlayheadSeconds(audioEngine.getPlayheadSeconds());
    mixer.refreshMeters();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // deviceManager avisa acá cualquier cambio de dispositivo, tipo,
    // sample rate o buffer -incluidos los que hace el usuario a mano desde
    // "Audio/MIDI..."-, así que basta con refrescar la etiqueta acá en vez
    // de sondearla en cada tick del timer de 15fps.
    updateLatencyLabel();
}

void MainComponent::updateLatencyLabel()
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const double sampleRate = device->getCurrentSampleRate();
        const int bufferSamples = device->getCurrentBufferSizeSamples();

        // Estimación de latencia de ida y vuelta: buffer del callback +
        // la latencia propia que reporte el driver de entrada/salida (en
        // WASAPI/ASIO suele incluir el "extra" que agrega el driver más
        // allá del tamaño de buffer pedido). No incluye el delay que
        // puedan sumar los propios plugins (getLatencySamples()) — ver
        // limitación en el README.
        const int roundTripSamples = bufferSamples
            + device->getOutputLatencyInSamples()
            + device->getInputLatencyInSamples();
        const double roundTripMs = sampleRate > 0.0 ? (roundTripSamples * 1000.0 / sampleRate) : 0.0;

        latencyLabel.setText(deviceManager.getCurrentAudioDeviceType()
                                  + "  ~" + juce::String(roundTripMs, 1) + " ms",
                              juce::dontSendNotification);
    }
    else
    {
        latencyLabel.setText("Sin dispositivo de audio", juce::dontSendNotification);
    }
}

void MainComponent::selectLowestLatencyDeviceType()
{
    // Orden de preferencia pensado para minimizar latencia sin depender de
    // SDKs propietarios que no vienen en el repo:
    //  1. ASIO: la mejor opción en Windows, pero solo existe en la lista
    //     si se compiló con JUCE_ASIO=1 (ver CMakeLists.txt) Y el usuario
    //     tiene instalado un driver ASIO real (el de su interfaz, o
    //     ASIO4ALL) — si no, este tipo directamente no aparece acá.
    //  2. WASAPI en modo exclusivo: evita que el mezclador de Windows
    //     agregue sus propios buffers extra por encima de los nuestros.
    //  3. WASAPI compartido: todavía mejor que DirectSound.
    //  4. DirectSound / CoreAudio / ALSA / JACK: lo que quede.
    static const char* priorityOrder[] =
    {
        "ASIO",
        "Windows Audio (Exclusive Mode)",
        "Windows Audio",
        "DirectSound",
        "CoreAudio",
        "JACK",
        "ALSA"
    };

    auto& availableTypes = deviceManager.getAvailableDeviceTypes();

    for (auto* wanted : priorityOrder)
    {
        for (auto* type : availableTypes)
        {
            if (type->getTypeName() == wanted)
            {
                // scanForDevices() asegura que el tipo tenga su lista de
                // dispositivos poblada antes de activarlo -si no, puede
                // no tener ningún dispositivo por defecto todavía-.
                type->scanForDevices();
                if (type->getDeviceNames().isEmpty())
                    break; // este tipo no tiene ni un dispositivo real; probar el siguiente

                deviceManager.setCurrentAudioDeviceType(type->getTypeName(), true);
                return;
            }
        }
    }
}

void MainComponent::preferAudioInterfaceDevice()
{
    // Se llama UNA sola vez al arrancar, después de elegir el tipo de
    // driver (ASIO/WASAPI/etc.) y antes de que el usuario haya tocado
    // nada a mano: si hay conectada una interfaz de audio dedicada (una
    // Focusrite Scarlett/Clarett, RME, PreSonus, MOTU, etc.) la preferimos
    // sobre el audio integrado del laptop (Realtek, "Speakers", el mic
    // interno) — mejores conversores, mejor latencia real, y es lo que un
    // músico que enchufa su interfaz espera que la app use sin tener que
    // ir a buscarla a mano en "Audio/MIDI...". Si el usuario después
    // cambia el dispositivo manualmente, esa elección queda como está:
    // esta función no vuelve a pisarla.
    auto* currentType = deviceManager.getCurrentDeviceTypeObject();
    if (currentType == nullptr)
        return;

    currentType->scanForDevices();

    // Nombres típicos de interfaces de audio reales conocidas. No hace
    // falta que la lista sea exhaustiva: si no matchea nada, simplemente
    // se deja lo que JUCE haya elegido por defecto.
    static const char* interfaceMarkers[] =
    {
        "Focusrite", "Scarlett", "Clarett", "Vocaster",
        "RME", "Fireface", "Babyface",
        "PreSonus", "AudioBox", "Quantum",
        "MOTU", "Zoom", "Behringer", "UMC",
        "Universal Audio", "Apollo",
        "Audient", "SSL", "Steinberg", "UR22", "UR44",
        "Native Instruments", "Komplete Audio"
    };

    // Wrappers/drivers genéricos: nunca se prefieren, aunque su nombre
    // coincida por accidente con algún marcador de arriba (ej. un
    // "ASIO4ALL" configurado sobre una Focusrite igual aparece con el
    // nombre genérico, no con "Focusrite").
    static const char* genericMarkers[] = { "ASIO4ALL", "Generic Low Latency" };

    auto findBestMatch = [&](const juce::StringArray& names) -> juce::String
    {
        for (auto& name : names)
        {
            bool isGeneric = false;
            for (auto* g : genericMarkers)
                if (name.containsIgnoreCase(g))
                    isGeneric = true;
            if (isGeneric)
                continue;

            for (auto* marker : interfaceMarkers)
                if (name.containsIgnoreCase(marker))
                    return name;
        }
        return {};
    };

    // Se buscan por separado (en vez de asumir el mismo nombre para
    // ambos): en ASIO un solo driver maneja entrada y salida y va a dar
    // el mismo nombre en las dos listas, pero en WASAPI son dispositivos
    // separados y podrían no coincidir exactamente en texto.
    const auto bestOutput = findBestMatch(currentType->getDeviceNames(false));
    const auto bestInput  = findBestMatch(currentType->getDeviceNames(true));

    if (bestOutput.isEmpty() && bestInput.isEmpty())
        return; // no hay ninguna interfaz conocida conectada; se deja lo que ya eligió JUCE

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup(setup);

    bool changed = false;
    if (bestOutput.isNotEmpty() && setup.outputDeviceName != bestOutput)
    {
        setup.outputDeviceName = bestOutput;
        changed = true;
    }
    if (bestInput.isNotEmpty() && setup.inputDeviceName != bestInput)
    {
        setup.inputDeviceName = bestInput;
        changed = true;
    }

    if (changed)
        deviceManager.setAudioDeviceSetup(setup, true);
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
