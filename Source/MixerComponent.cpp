#include "MixerComponent.h"

// ---------------------------------------------------------------- Channel

MixerChannel::MixerChannel(juce::String name, int index, PluginHost& host, AudioEngine& engine)
    : pluginHost(host), audioEngine(engine), channelIndex(index), channelName(std::move(name))
{
    audioEngine.ensureChannelCount(channelIndex + 1);

    nameLabel.setText(channelName, juce::dontSendNotification);
    nameLabel.setJustificationType(juce::Justification::centred);
    nameLabel.setFont(juce::Font(12.0f));
    addAndMakeVisible(nameLabel);

    volumeFader.setRange(0.0, 1.5, 0.01);
    volumeFader.setValue(1.0);
    volumeFader.onValueChange = [this]
    {
        audioEngine.setChannelGain(channelIndex, (float) volumeFader.getValue());
    };
    addAndMakeVisible(volumeFader);

    muteButton.setClickingTogglesState(true);
    muteButton.onClick = [this]
    {
        audioEngine.setChannelMute(channelIndex, muteButton.getToggleState());
    };
    addAndMakeVisible(muteButton);

    soloButton.setClickingTogglesState(true);
    soloButton.onClick = [this]
    {
        audioEngine.setChannelSolo(channelIndex, soloButton.getToggleState());
    };
    addAndMakeVisible(soloButton);

    pluginSlotButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
    pluginSlotButton.onClick = [this] { showPluginChainMenu(); };
    addAndMakeVisible(pluginSlotButton);

    inputSlotButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2f3a3a));
    inputSlotButton.onClick = [this] { showInputMenu(); };
    addAndMakeVisible(inputSlotButton);
}

void MixerChannel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(getLocalBounds().reduced(2));

    auto meterArea = getLocalBounds().reduced(2).removeFromRight(10).reduced(2);
    g.setColour(juce::Colours::darkgrey);
    g.fillRect(meterArea);

    auto filled = meterArea.withY(meterArea.getBottom() - (int) (meterArea.getHeight() * currentLevel))
                            .withHeight((int) (meterArea.getHeight() * currentLevel));
    g.setColour(currentLevel > 0.85f ? juce::Colours::red : juce::Colours::limegreen);
    g.fillRect(filled);
}

void MixerChannel::resized()
{
    auto area = getLocalBounds().reduced(4);
    nameLabel.setBounds(area.removeFromTop(18));

    auto slotRow = area.removeFromTop(22);
    inputSlotButton.setBounds(slotRow.removeFromLeft(slotRow.getWidth() / 2).reduced(1));
    pluginSlotButton.setBounds(slotRow.reduced(1));

    auto buttonsRow = area.removeFromBottom(22);
    muteButton.setBounds(buttonsRow.removeFromLeft(buttonsRow.getWidth() / 2).reduced(1));
    soloButton.setBounds(buttonsRow.reduced(1));

    area.removeFromRight(16);
    volumeFader.setBounds(area);
}

// ------------------------------------------------------- Editor de plugin

MixerChannel::PluginEditorWindow::PluginEditorWindow(juce::AudioPluginInstance& pluginToShow,
                                                       std::function<void()> onCloseCallback)
    : juce::DocumentWindow(pluginToShow.getName(), juce::Colour(0xff2a2a2a),
                            juce::DocumentWindow::closeButton)
    , plugin(&pluginToShow)
    , onClose(std::move(onCloseCallback))
{
    setUsingNativeTitleBar(true);

    // Ya se chequeó hasEditor() antes de crear esta ventana (ver
    // openPluginEditor), pero por las dudas nunca se llama a un plugin sin
    // GUI hasta acá; createEditorIfNeeded es lo que realmente paga el
    // costo de crear la GUI nativa, recién ahora y no antes.
    if (auto* editor = pluginToShow.createEditorIfNeeded())
        setContentOwned(editor, true);
    else
        setContentOwned(new juce::Label({}, "Este plugin no tiene interfaz grafica propia."), true);

    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MixerChannel::PluginEditorWindow::closeButtonPressed()
{
    // Dispara el callback del dueño (MixerChannel), que borra este objeto
    // de openEditorWindows -y con eso, a este mismo objeto- desde afuera.
    // Es seguro: después de esta llamada no se vuelve a tocar ningún
    // miembro de la ventana.
    if (onClose)
        onClose();
}

// ---------------------------------------------------------- Cadena por canal

void MixerChannel::addPluginToChain(const juce::PluginDescription& description)
{
    // Instanciar con el sample rate/block size REALES del dispositivo en
    // uso (no un 44100/1024 fijo): si el usuario bajó la latencia con un
    // buffer más chico o su interfaz corre a otro sample rate, cargar el
    // plugin con los valores equivocados puede sonar mal o directamente
    // agregar latencia extra hasta el próximo prepareToPlay.
    auto instance = pluginHost.createInstance(description,
        audioEngine.getCurrentSampleRate(), audioEngine.getCurrentBlockSize());

    if (instance == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Plugin", "No se pudo cargar \"" + description.name + "\".");
        return;
    }

    // CRÍTICO: crear la instancia no la deja lista para procesar. El
    // sampleRate/blockSize que le pasamos arriba a createInstance solo
    // sirve para instanciarla con esos valores "de referencia" — recién
    // prepareToPlay() activa de verdad el AudioProcessor (en VST3 esto
    // dispara IComponent::setActive(true) puertas adentro). El único
    // lugar que llamaba a prepareToPlay() era
    // AudioEngine::audioDeviceAboutToStart(), que corre una sola vez al
    // arrancar el dispositivo de audio — cualquier plugin agregado
    // DESPUÉS de eso (o sea, casi siempre, ya que el audio arranca apenas
    // abre la app) nunca quedaba activado y procesaba en silencio, como
    // si estuviera apagado, aunque su GUI se viera normal.
    instance->prepareToPlay(audioEngine.getCurrentSampleRate(), audioEngine.getCurrentBlockSize());

    // El canal sigue siendo el dueño real de la instancia (pluginChain);
    // el engine solo recibe punteros crudos + el flag de bypass vía
    // syncChainToEngine().
    auto* slot = new LoadedPlugin();
    slot->instance = std::move(instance);
    pluginChain.add(slot);
    syncChainToEngine();
    updatePluginButtonText();
}

void MixerChannel::removePluginFromChain(int index)
{
    if (index < 0 || index >= pluginChain.size())
        return;

    auto* plugin = pluginChain.getUnchecked(index)->instance.get();

    // Si la GUI de este plugin está abierta, cerrarla primero: no tiene
    // sentido dejar una ventana viva apuntando a un AudioProcessor que
    // está por desaparecer.
    for (int i = openEditorWindows.size(); --i >= 0;)
        if (openEditorWindows.getUnchecked(i)->plugin == plugin)
            openEditorWindows.remove(i);

    plugin->releaseResources();
    pluginChain.remove(index, true); // true = borra el LoadedPlugin (y con él, la instancia)
    syncChainToEngine();
    updatePluginButtonText();
}

void MixerChannel::movePluginInChain(int index, int delta)
{
    const int newIndex = index + delta;
    if (index < 0 || index >= pluginChain.size() || newIndex < 0 || newIndex >= pluginChain.size())
        return;

    pluginChain.move(index, newIndex);
    syncChainToEngine();
}

void MixerChannel::openPluginEditor(int index)
{
    if (index < 0 || index >= pluginChain.size())
        return;

    auto* plugin = pluginChain.getUnchecked(index)->instance.get();

    // Si ya está abierta la GUI de este plugin, solo traerla al frente en
    // vez de crear una segunda ventana para la misma instancia.
    for (auto* win : openEditorWindows)
    {
        if (win->plugin == plugin)
        {
            win->toFront(true);
            return;
        }
    }

    if (!plugin->hasEditor())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Sin GUI", "\"" + plugin->getName() + "\" no tiene interfaz gráfica propia.");
        return;
    }

    auto* newWindow = new PluginEditorWindow(*plugin, [this, plugin]
    {
        for (int i = openEditorWindows.size(); --i >= 0;)
            if (openEditorWindows.getUnchecked(i)->plugin == plugin)
                openEditorWindows.remove(i);
    });
    openEditorWindows.add(newWindow);
}

void MixerChannel::togglePluginBypass(int index)
{
    if (index < 0 || index >= pluginChain.size())
        return;

    // Apagar/prender no recrea ni reconecta nada: solo cambia el flag que
    // AudioEngine chequea antes de llamar a processBlock de ese eslabón
    // (ver AudioEngine::PluginSlot). El plugin sigue cargado con su
    // estado interno intacto, así que "prenderlo" de nuevo no pierde
    // ningún parámetro que el usuario haya tocado.
    auto* slot = pluginChain.getUnchecked(index);
    slot->bypassed = !slot->bypassed;
    syncChainToEngine();
}

void MixerChannel::syncChainToEngine()
{
    juce::Array<AudioEngine::PluginSlot> rawChain;
    for (auto* slot : pluginChain)
        rawChain.add({ slot->instance.get(), slot->bypassed });
    audioEngine.setChannelPluginChain(channelIndex, rawChain);
}

void MixerChannel::updatePluginButtonText()
{
    pluginSlotButton.setButtonText("Plugins (" + juce::String(pluginChain.size()) + ")");
}

void MixerChannel::refreshMeter()
{
    currentLevel = juce::jlimit(0.0f, 1.0f, audioEngine.getChannelLevel(channelIndex) * 4.0f);
    repaint();
}

void MixerChannel::applyState(float gain, bool muted, bool solo)
{
    volumeFader.setValue(gain, juce::dontSendNotification);
    muteButton.setToggleState(muted, juce::dontSendNotification);
    soloButton.setToggleState(solo, juce::dontSendNotification);

    audioEngine.setChannelGain(channelIndex, gain);
    audioEngine.setChannelMute(channelIndex, muted);
    audioEngine.setChannelSolo(channelIndex, solo);
}

void MixerChannel::showPluginChainMenu()
{
    // Lista lo YA escaneado (PluginHost::getKnownPlugins) — no vuelve a
    // tocar el disco, así que abrir el menú es instantáneo.
    auto descriptions = pluginHost.getKnownPlugins();

    juce::PopupMenu menu;

    // --- Agregar un plugin nuevo al final de la cadena -------------------
    juce::PopupMenu addMenu;
    if (descriptions.isEmpty())
        addMenu.addItem(1, "Sin plugins escaneados (usa \"Escanear VST3...\")", false);
    else
        for (int i = 0; i < descriptions.size(); ++i)
            addMenu.addItem(i + 1, descriptions[i].name);
    menu.addSubMenu("+ Agregar plugin", addMenu);

    // --- Plugins ya cargados en la cadena, en orden de procesamiento -----
    // Cada uno tiene su propio submenú con: ver su GUI nativa, bypass
    // (encendido/apagado), subirlo o bajarlo un lugar en la cadena, o
    // quitarlo del canal. Codificación de ids: 10000 + índice*10 + acción
    // (1=GUI, 2=subir, 3=bajar, 4=quitar, 5=bypass).
    if (!pluginChain.isEmpty())
    {
        menu.addSeparator();
        for (int i = 0; i < pluginChain.size(); ++i)
        {
            auto* slot = pluginChain.getUnchecked(i);
            const int base = 10000 + i * 10;

            juce::PopupMenu itemMenu;
            itemMenu.addItem(base + 5, slot->bypassed ? "Encender" : "Apagar (bypass)",
                              true, slot->bypassed);
            itemMenu.addSeparator();
            itemMenu.addItem(base + 1, "Ver GUI", slot->instance->hasEditor());
            itemMenu.addItem(base + 2, "Subir en la cadena", i > 0);
            itemMenu.addItem(base + 3, "Bajar en la cadena", i < pluginChain.size() - 1);
            itemMenu.addSeparator();
            itemMenu.addItem(base + 4, "Quitar de este canal");

            // El estado se ve de un vistazo en el propio título del
            // submenú, sin tener que abrirlo: "[OFF]" cuando está
            // bypasseado.
            menu.addSubMenu(juce::String(i + 1) + ". " + slot->instance->getName()
                                 + (slot->bypassed ? "  [OFF]" : ""),
                             itemMenu);
        }
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(pluginSlotButton),
        [this, descriptions](int result)
        {
            if (result == 0)
                return;

            if (result >= 1 && result <= descriptions.size())
            {
                addPluginToChain(descriptions[result - 1]);
                return;
            }

            if (result >= 10000)
            {
                const int index = (result - 10000) / 10;
                const int action = (result - 10000) % 10;

                switch (action)
                {
                    case 1: openPluginEditor(index);        break;
                    case 2: movePluginInChain(index, -1);   break;
                    case 3: movePluginInChain(index, +1);   break;
                    case 4: removePluginFromChain(index);   break;
                    case 5: togglePluginBypass(index);      break;
                    default: break;
                }
            }
        });
}

void MixerChannel::showInputMenu()
{
    // Se puebla al momento de abrir el menú (no en el constructor) para
    // reflejar la interfaz de audio actual: si el usuario la cambió desde
    // "Audio/MIDI...", la cantidad de entradas puede haber cambiado.
    const int numInputs = audioEngine.getNumHardwareInputChannels();

    juce::PopupMenu menu;
    menu.addItem(1, "Ninguna (sin entrada en vivo)");

    if (numInputs == 0)
    {
        menu.addItem(2, "La interfaz actual no tiene entradas activas"
                         " — revisá \"Audio/MIDI...\"", false);
    }
    else
    {
        for (int i = 0; i < numInputs; ++i)
            menu.addItem(i + 100, "Entrada " + juce::String(i + 1));
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(inputSlotButton),
        [this](int result)
        {
            if (result == 1)
            {
                audioEngine.setChannelInput(channelIndex, -1);
                inputSlotButton.setButtonText("In: -");
            }
            else if (result >= 100)
            {
                const int hardwareChannel = result - 100;
                audioEngine.setChannelInput(channelIndex, hardwareChannel);
                inputSlotButton.setButtonText("In: " + juce::String(hardwareChannel + 1));
            }
        });
}

// ------------------------------------------------------------------ Mixer

MixerComponent::MixerComponent(PluginHost& host, AudioEngine& engine)
    : pluginHost(host), audioEngine(engine)
{
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&channelHolder, false);
    viewport.setScrollBarsShown(false, true);

    addTrackChannel("Canal 1");
    addTrackChannel("Canal 2");
}

void MixerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181818));
}

void MixerComponent::resized()
{
    viewport.setBounds(getLocalBounds());
    layoutChannels();
}

void MixerComponent::layoutChannels()
{
    channelHolder.setSize(channelWidth * juce::jmax(1, channels.size()), viewport.getHeight());

    int x = 0;
    for (auto* ch : channels)
    {
        ch->setBounds(x, 0, channelWidth, channelHolder.getHeight());
        x += channelWidth;
    }
}

void MixerComponent::addTrackChannel(const juce::String& name)
{
    auto* newChannel = new MixerChannel(name, channels.size(), pluginHost, audioEngine);
    channels.add(newChannel);
    channelHolder.addAndMakeVisible(newChannel);
    layoutChannels();
}

void MixerComponent::addChannelWithPlugin(const juce::PluginDescription& description)
{
    auto* newChannel = new MixerChannel(description.name, channels.size(), pluginHost, audioEngine);
    newChannel->addPluginToChain(description);
    channels.add(newChannel);
    channelHolder.addAndMakeVisible(newChannel);
    layoutChannels();
}

void MixerComponent::refreshMeters()
{
    for (auto* ch : channels)
        ch->refreshMeter();
}

void MixerComponent::getChannelState(int index, float& gain, bool& muted, bool& solo) const
{
    if (index >= 0 && index < channels.size())
    {
        gain = channels.getUnchecked(index)->getGain();
        muted = channels.getUnchecked(index)->getMuted();
        solo = channels.getUnchecked(index)->getSolo();
    }
    else
    {
        gain = 1.0f;
        muted = false;
        solo = false;
    }
}

void MixerComponent::applyChannelState(int index, float gain, bool muted, bool solo)
{
    if (index >= 0 && index < channels.size())
        channels.getUnchecked(index)->applyState(gain, muted, solo);
}

void MixerComponent::clearAll()
{
    channels.clear();
    layoutChannels();
}
