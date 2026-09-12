#include "ProjectState.h"

namespace ProjectFile
{
    bool save(const juce::File& file, const ProjectData& data)
    {
        juce::XmlElement root("LITEDAW_PROJECT");
        root.setAttribute("formatVersion", 1);

        auto* tracksXml = root.createNewChildElement("TRACKS");
        for (auto& name : data.trackNames)
        {
            auto* t = tracksXml->createNewChildElement("TRACK");
            t->setAttribute("name", name);
        }

        auto* clipsXml = root.createNewChildElement("CLIPS");
        for (auto& clip : data.clips)
        {
            auto* c = clipsXml->createNewChildElement("CLIP");
            c->setAttribute("name", clip.name);
            c->setAttribute("file", clip.filePath);
            c->setAttribute("start", clip.startSeconds);
            c->setAttribute("length", clip.lengthSeconds);
            c->setAttribute("track", clip.trackIndex);
        }

        auto* channelsXml = root.createNewChildElement("CHANNELS");
        for (auto& channel : data.channels)
        {
            auto* ch = channelsXml->createNewChildElement("CHANNEL");
            ch->setAttribute("gain", channel.gain);
            ch->setAttribute("muted", channel.muted);
            ch->setAttribute("solo", channel.solo);
        }

        return root.writeTo(file);
    }

    bool load(const juce::File& file, ProjectData& outData)
    {
        auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr || xml->getTagName() != "LITEDAW_PROJECT")
            return false;

        outData = ProjectData();

        if (auto* tracksXml = xml->getChildByName("TRACKS"))
            for (auto* t : tracksXml->getChildIterator())
                outData.trackNames.add(t->getStringAttribute("name"));

        if (auto* clipsXml = xml->getChildByName("CLIPS"))
        {
            for (auto* c : clipsXml->getChildIterator())
            {
                ProjectClipData clip;
                clip.name = c->getStringAttribute("name");
                clip.filePath = c->getStringAttribute("file");
                clip.startSeconds = c->getDoubleAttribute("start");
                clip.lengthSeconds = c->getDoubleAttribute("length");
                clip.trackIndex = c->getIntAttribute("track");
                outData.clips.add(clip);
            }
        }

        if (auto* channelsXml = xml->getChildByName("CHANNELS"))
        {
            for (auto* ch : channelsXml->getChildIterator())
            {
                ProjectChannelData channel;
                channel.gain = (float) ch->getDoubleAttribute("gain", 1.0);
                channel.muted = ch->getBoolAttribute("muted");
                channel.solo = ch->getBoolAttribute("solo");
                outData.channels.add(channel);
            }
        }

        return true;
    }
}
