#include "FleetGame.h"

#include <karl/sdk.h>
#include <nlohmann/json.hpp>

#include <cstring>

namespace
{
    using karl::sdk::Host;
    using karl::sdk::Input;

    void *create(const Host *host, osg::Group *root, const char *config)
    {
        // "mode": "sea" (default: open sea, fleets from every side) or
        // "canal" (one canal across the middle, west to east).
        const nlohmann::json json = nlohmann::json::parse(config, nullptr, false);
        const std::string mode = json.is_object() ? json.value("mode", "sea") : "sea";
        return new fleet::FleetGame(host, root, mode);
    }

    void frame(void *self, const Input *input, float dt)
    {
        static_cast<fleet::FleetGame *>(self)->frame(*input, dt);
    }

    void destroy(void *self)
    {
        delete static_cast<fleet::FleetGame *>(self);
    }

    const karl::sdk::Surface SURFACE{
        sizeof(karl::sdk::Surface),
        &create,
        &frame,
        nullptr,
        nullptr,
        &destroy,
        nullptr,
        nullptr,
    };

    const karl::sdk::Surface *surface(const char *type)
    {
        return std::strcmp(type, "FleetGame") == 0 ? &SURFACE : nullptr;
    }

    const char *const TYPES[] = {"FleetGame", nullptr};

    // The whole page: every knob, and the right column and bottom row keys it
    // lays out itself. README, Manifest.
    const char *const MANIFEST = R"json({
  "label": "Fleet Game",
  "description": "A naval tower defence on the whole panel: each knob aims a lighthouse at passing fleets.",
  "category": "tools",
  "controls": [{
    "type": "FleetGame",
    "name": "Fleet Game",
    "description": "The game on the whole page.",
    "footprint": {"page": true},
    "reserved": [
      ["BUTTON_RIGHT", 0], ["BUTTON_RIGHT", 1], ["BUTTON_RIGHT", 3], ["BUTTON_RIGHT", 4],
      ["BUTTON_RIGHT", 5], ["BUTTON_RIGHT", 6], ["BUTTON_RIGHT", 7],
      ["BUTTON_BOTTOM", 0], ["BUTTON_BOTTOM", 1], ["BUTTON_BOTTOM", 2], ["BUTTON_BOTTOM", 3], ["BUTTON_BOTTOM", 4],
      ["BUTTON_BOTTOM", 5], ["BUTTON_BOTTOM", 6], ["BUTTON_BOTTOM", 7], ["BUTTON_BOTTOM", 8], ["BUTTON_BOTTOM", 9]
    ],
    "fields": [
      {"name": "mode", "label": "Mode", "type": "select", "options": ["sea", "canal"], "default": "sea"}
    ]
  }]
})json";

    const karl::sdk::Plugin PLUGIN{
        sizeof(karl::sdk::Plugin),
        KARL_SDK_ABI,
        "fleet-game",
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
