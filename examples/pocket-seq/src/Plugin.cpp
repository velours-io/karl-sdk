#include "PocketSeq.h"

#include <karl/sdk.h>
#include <nlohmann/json.hpp>

#include <cstring>

namespace
{
    // controls.json, all optional (the configurator writes them from the
    // manifest below):
    //   "id": "pocket-seq"  (names the saved pattern),
    //   "sync": "internal" | "midi" | "live"  (ALT + PLAY cycles; the last one
    //     used is saved with the pattern and wins over this),
    //   "bpm": 120  (internal), "midiPort": 0, "midiChannel": 1,
    //   "link" + "span": the 2x2 knobs, "keys": {"play": [...], "step": [...]}.
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
        config.port = json.value("midiPort", config.port);
        config.channel = json.value("midiChannel", config.channel);
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

    // What the configurator offers and how it places it: a 2x2 block of knobs
    // and two big keys, plus the options below. README, Manifest.
    const char *const MANIFEST = R"json({
  "label": "Pocket Seq",
  "description": "A 16 step sequencer on four knobs and two keys, next to the rest of the page.",
  "category": "midi",
  "controls": [{
    "type": "PocketSeq",
    "name": "Pocket Seq",
    "description": "Step, pitch, velocity and octave on a 2x2 block of knobs; PLAY and STEP on two big keys.",
    "footprint": {"knobs": [2, 2]},
    "keys": [
      {"role": "play", "label": "PLAY / STOP", "size": 2},
      {"role": "step", "label": "STEP ON / OFF", "size": 2}
    ],
    "fields": [
      {"name": "sync", "label": "Sync", "type": "select", "options": ["internal", "midi", "live"], "default": "internal", "group": "Clock"},
      {"name": "bpm", "label": "Tempo (BPM, internal)", "type": "number", "default": 120, "min": 20, "max": 300, "group": "Clock"},
      {"name": "midiPort", "label": "Port", "type": "number", "default": 0, "min": 0, "max": 1, "group": "MIDI"},
      {"name": "midiChannel", "label": "Channel (1-16)", "type": "number", "default": 1, "min": 1, "max": 16, "group": "MIDI"}
    ]
  }]
})json";

    const karl::sdk::Plugin PLUGIN{
        sizeof(karl::sdk::Plugin),
        KARL_SDK_ABI,
        "pocket-seq",
        "1.1.0",
        TYPES,
        &surface,
        MANIFEST,
    };
}

KARL_PLUGIN_EXPORT const karl::sdk::Plugin *karl_plugin()
{
    return &PLUGIN;
}
