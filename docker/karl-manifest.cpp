// Prints a plugin library's name and manifest as JSON, for karl-sim to build
// a page when the project brings no sim.json:
//   karl-manifest plugin.so -> {"name": "...", "manifest": {...} | null}
#include <karl/sdk.h>

#include <cstdio>
#include <dlfcn.h>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: karl-manifest <plugin.so>\n");
        return 2;
    }
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "karl-manifest: %s\n", dlerror());
        return 1;
    }
    using Entry = const karl::sdk::Plugin *(*)();
    auto entry = reinterpret_cast<Entry>(dlsym(library, "karl_plugin"));
    const karl::sdk::Plugin *plugin = entry ? entry() : nullptr;
    if (!plugin || !plugin->name)
    {
        std::fprintf(stderr, "karl-manifest: not a KARL plugin\n");
        return 1;
    }
    const bool hasManifest = plugin->size >= offsetof(karl::sdk::Plugin, manifest) + sizeof(plugin->manifest) && plugin->manifest;
    std::printf("{\"name\": \"%s\", \"manifest\": %s}\n", plugin->name, hasManifest ? plugin->manifest : "null");
    return 0;
}
