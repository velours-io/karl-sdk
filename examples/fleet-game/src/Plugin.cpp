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
    };

    const karl::sdk::Surface *surface(const char *type)
    {
        return std::strcmp(type, "FleetGame") == 0 ? &SURFACE : nullptr;
    }

    const char *const TYPES[] = {"FleetGame", nullptr};

    const karl::sdk::Plugin PLUGIN{
        sizeof(karl::sdk::Plugin),
        KARL_SDK_ABI,
        "fleet-game",
        "1.0.0",
        TYPES,
        &surface,
    };
}

KARL_PLUGIN_EXPORT const karl::sdk::Plugin *karl_plugin()
{
    return &PLUGIN;
}
