#include "PocketSeq.h"

#include <karl/sdk.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <map>

namespace
{
    using karl::sdk::Edge;

    Edge edgeOf(const std::string &name, Edge fallback)
    {
        static const std::map<std::string, Edge> EDGES = {
            {"BUTTON_LEFT", Edge::LEFT}, {"BUTTON_RIGHT", Edge::RIGHT},
            {"BUTTON_TOP", Edge::TOP}, {"BUTTON_BOTTOM", Edge::BOTTOM}};
        const auto it = EDGES.find(name);
        return it == EDGES.end() ? fallback : it->second;
    }

    // controls.json, all optional:
    //   "id": "pocket-seq"  (names the saved pattern),
    //   "sync": "internal" | "midi" | "live"  (ALT + PLAY cycles; the last one
    //     used is saved with the pattern and wins over this),
    //   "bpm": 120  (internal),
    //   "midi": {"port": 0, "channel": 1},
    //   "keys": {"edge": "BUTTON_BOTTOM", "play": 0, "step": 2}  (big keys: even slots)
    pocket::Config readConfig(const char *text)
    {
        pocket::Config config;
        const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
        if (!json.is_object())
        {
            return config;
        }
        config.id = json.value("id", config.id);
        const std::string sync = json.value("sync", std::string("internal"));
        config.sync = sync == "midi" ? pocket::Sync::MIDI : sync == "live" ? pocket::Sync::LIVE : pocket::Sync::INTERNAL;
        config.bpm = json.value("bpm", config.bpm);
        if (json.contains("midi") && json["midi"].is_object())
        {
            config.port = json["midi"].value("port", config.port);
            config.channel = json["midi"].value("channel", config.channel);
        }
        if (json.contains("keys") && json["keys"].is_object())
        {
            const auto &keys = json["keys"];
            config.keysEdge = edgeOf(keys.value("edge", std::string()), config.keysEdge);
            config.playSlot = keys.value("play", config.playSlot);
            config.stepSlot = keys.value("step", config.stepSlot);
        }
        return config;
    }

    void *create(const karl::sdk::Host *host, osg::Group *root, const char *config)
    {
        return new pocket::PocketSeq(host, root, readConfig(config));
    }

    void frame(void *self, const karl::sdk::Input *input, float dt)
    {
        static_cast<pocket::PocketSeq *>(self)->frame(*input, dt);
    }

    void tick(void *self, float dt)
    {
        static_cast<pocket::PocketSeq *>(self)->tick(dt);
    }

    void clock(void *self, int tick)
    {
        static_cast<pocket::PocketSeq *>(self)->clock(tick);
    }

    void event(void *self, const char *name, const char *json)
    {
        static_cast<pocket::PocketSeq *>(self)->event(name, json ? json : "");
    }

    void destroy(void *self)
    {
        delete static_cast<pocket::PocketSeq *>(self);
    }

    karl::sdk::Surface makeSurface()
    {
        karl::sdk::Surface surface{};
        surface.size = sizeof(karl::sdk::Surface);
        surface.create = &create;
        surface.frame = &frame;
        surface.tick = &tick;
        surface.clock = &clock;
        surface.event = &event;
        surface.destroy = &destroy;
        return surface;
    }

    const karl::sdk::Surface SURFACE = makeSurface();

    const karl::sdk::Surface *surface(const char *type)
    {
        return std::strcmp(type, "PocketSeq") == 0 ? &SURFACE : nullptr;
    }

    const char *const TYPES[] = {"PocketSeq", nullptr};

    const karl::sdk::Plugin PLUGIN{
        sizeof(karl::sdk::Plugin),
        KARL_SDK_ABI,
        "pocket-seq",
        "1.0.0",
        TYPES,
        &surface,
    };
}

KARL_PLUGIN_EXPORT const karl::sdk::Plugin *karl_plugin()
{
    return &PLUGIN;
}
