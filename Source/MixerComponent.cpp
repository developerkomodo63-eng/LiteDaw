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
    pluginSlotButton.onClick = [this] { showPluginMenu(); };
    addAndMakeVisible(pluginSlotButton);
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
    pluginSlotButton.setBounds(area.removeFromTop(22));

    auto buttonsRow = area.removeFromBottom(22);
    muteButton.setBounds(buttonsRow.removeFromLeft(buttonsRow.getWidth() / 2).reduced(1));
    soloButton.setBounds(buttonsRow.reduced(1));

    area.removeFromRight(16);
    volumeFader.setBounds(area);
}

void MixerChannel::loadPlugin(const juce::PluginDescription& description)
{
    pluginInstance = pluginHost.createInstance(description, 44100.0, 1024);
    pluginSlotButton.setButtonText(pluginInstance != nullptr ? description.name : "(error)");

    // El engine solo guarda un puntero crudo (no ownership): el canal
    // sigue siendo el dueño de la instancia y decide cuándo destruirla.
    audioEngine.setChannelPlugin(channelIndex, pluginInstance.get());
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

void MixerChannel::showPluginMenu()
{
    // Lista lo YA escaneado (PluginHost::getKnownPlugins) — no vuelve a
    // tocar el disco, así que abrir el menú es instantáneo.
    auto descriptions = pluginHost.getKnownPlugins();

    juce::PopupMenu menu;
    if (descriptions.isEmpty())
    {
        menu.addItem(1, "Sin plugins escaneados (usa \"Escanear VST3...\")", false);
    }
    else
    {
        int itemId = 1;
        for (auto& d : descriptions)
            menu.addItem(itemId++, d.name);
    }

    if (pluginInstance != nullptr)
    {
        menu.addSeparator();
        menu.addItem(9000, "Quitar plugin de este canal");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(pluginSlotButton),
        [this, descriptions](int result)
        {
            if (result == 0)
                return;

            if (result == 9000)
            {
                unloadPlugin();
                return;
            }

            if (result >= 1 && result <= descriptions.size())
                loadPlugin(descriptions[result - 1]);
        });
}

void MixerChannel::unloadPlugin()
{
    pluginInstance.reset();
    pluginSlotButton.setButtonText("(vacío)");
    audioEngine.setChannelPlugin(channelIndex, nullptr);
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
    newChannel->loadPlugin(description);
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
