// KARL plugin SDK: the whole contract between KARL and a plugin.
//
// A plugin is a shared library (.so on the unit) that exports karl_plugin().
// It draws with OpenSceneGraph under the root it is given and talks to KARL
// only through the tables below. Only plain data, const char * and osg types
// cross the boundary, so the plugin's own STL or json library never matters.
//
// The tables are append-only: KARL adds functions at the end and sets `size`,
// so a plugin built against an older header keeps working. KARL_SDK_ABI only
// changes when the compiler or the OSG version of the OS image does.
//
// Everything runs on KARL's UI thread. Strings passed in either direction are
// only valid for the duration of the call.
#pragma once

#include <cstdint>

namespace osg
{
    class Group;
}
namespace osgText
{
    class Text;
}

#define KARL_SDK_ABI 1
// This header's release. The minor number grows with what the tables gain
// (1.1: Surface::tick and Surface::clock, Live transport events); a KARL
// older than a field leaves it unused, which the size checks make safe.
#define KARL_SDK_VERSION "1.1.0"

#if defined(_WIN32)
#define KARL_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define KARL_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace karl::sdk
{
    constexpr int ENCODERS = 24;
    constexpr int KEYS = 44;
    // The display, in KARL's units: x right, y up, (0, 0) bottom left.
    constexpr float DISPLAY_WIDTH = 1280.0f;
    constexpr float DISPLAY_HEIGHT = 800.0f;
    // A full turn of a knob, in Input::angle units.
    constexpr int ANGLE_RESOLUTION = 16384;

    // Where a key sits: edge and slot, as BUTTON_LEFT etc. in controls.json.
    // The corners are ALT (slot 0 or 1); a plugin can still claim them.
    enum class Edge : uint8_t
    {
        LEFT,
        RIGHT,
        TOP,
        BOTTOM,
        CORNER_TOP,
        CORNER_BOTTOM,
    };

    enum class KeyState : uint8_t
    {
        IDLE,
        PUSHED,       // the frame it went down
        PRESSED,      // held
        RELEASED,     // the frame it came up
        LONG_PUSHED,  // the frame a hold reached 400 ms
        LONG_PRESSED, // held past that
    };

    enum class Font : uint8_t
    {
        REGULAR,
        BOLD,
    };

    struct Rgb
    {
        uint8_t r, g, b;
    };

    // One frame of hardware, every key and knob, claimed or not.
    struct Input
    {
        // Absolute knob angle, 0..ANGLE_RESOLUTION-1, -1 = no reading yet.
        int32_t angle[ENCODERS];
        // Steps since the last frame.
        int32_t delta[ENCODERS];
        // By key index (Host::keyIndex).
        KeyState key[KEYS];
        // A corner key is held.
        bool alt;
    };

    enum class MidiType : uint8_t
    {
        CC,
        NOTE, // value = velocity, 0 = off
        PROGRAM_CHANGE,
    };

    struct MidiMessage
    {
        uint8_t port;
        MidiType type;
        uint8_t channel; // 1..16
        uint8_t number;  // CC, note or program
        uint8_t value;
    };

    // What KARL offers one surface. Every function takes this table first.
    // Valid from Surface::create until Surface::destroy.
    struct Host
    {
        uint32_t size;
        // KARL's own. Don't touch.
        void *context;

        // The surface's size, from controls.json ("width" / "height"),
        // the whole display by default. The root draws in 0..width, 0..height
        // and nothing outside it shows.
        float width;
        float height;

        // Layout. Indices are fixed by the hardware; ask, don't hard-code.
        // -1 when there is no such key or knob.
        int (*keyIndex)(const Host *host, Edge edge, int slot);
        int (*encoderIndex)(const Host *host, int row, int col); // row 0 = top
        // The middle of a knob, where KARL draws its own rings, in the
        // surface's coordinates.
        bool (*encoderCenter)(const Host *host, int encoder, float *x, float *y);

        // Keys. A claimed key is the plugin's: KARL's own control on it, if
        // the page has one, goes quiet and hidden until it is released, and
        // the key shows the plugin's label and color. `span` 2 makes a big
        // key over this slot and the next (start it on an even slot).
        // Claims end with the surface.
        void (*claimKey)(const Host *host, int key, int span);
        void (*releaseKey)(const Host *host, int key);
        // On a claimed key only. The color is also the key's LED.
        void (*setLabel)(const Host *host, int key, const char *text);
        void (*setColor)(const Host *host, int key, Rgb color);

        // Knobs. A claimed knob no longer moves KARL's own control on it.
        void (*claimEncoder)(const Host *host, int encoder);
        void (*releaseEncoder)(const Host *host, int encoder);
        // The empty squares KARL draws under every knob. Shown again when the
        // surface goes.
        void (*showEncoderBackground)(const Host *host, bool show);

        // Text. makeText gives an osgText::Text in KARL's look, ready to add
        // under the root; hold it in an osg::ref_ptr. fontPath is for
        // styling text yourself with osgText.
        osgText::Text *(*makeText)(const Host *host, const char *text, float size, Font font);
        const char *(*fontPath)(const Host *host, Font font);

        // MIDI out. Ports as in controls.json, channel 1..16.
        void (*sendCC)(const Host *host, int port, int channel, int cc, int value);
        void (*sendNote)(const Host *host, int port, int channel, int note, int velocity);
        void (*sendMidi)(const Host *host, int port, const uint8_t *bytes, int length);
        // OSC out, through the configurator, to a target as in controls.json.
        void (*sendOsc)(const Host *host, const char *target, const char *address, float value);

        // KARL's events. A subscribed event comes in through Surface::event.
        void (*subscribe)(const Host *host, const char *event);
        void (*publish)(const Host *host, const char *event, const char *json);
        void (*goToPage)(const Host *host, const char *pageId);

        void (*log)(const Host *host, const char *message);
        // A folder that is the plugin's alone and survives restarts.
        const char *(*dataDir)(const Host *host);
    };

    // One control type. KARL makes one instance per control of that type in
    // controls.json, and keeps it while its page exists.
    struct Surface
    {
        uint32_t size;
        // `config` is the control's object from controls.json, as JSON.
        // Return the instance, or null when it cannot start.
        void *(*create)(const Host *host, osg::Group *root, const char *config);
        // Once per frame while its page shows. `dt` in seconds.
        void (*frame)(void *self, const Input *input, float dt);
        // Every incoming message, page showing or not. May be null.
        void (*midiIn)(void *self, const MidiMessage *message);
        // A subscribed event. `json` is its data. May be null.
        void (*event)(void *self, const char *name, const char *json);
        void (*destroy)(void *self);
        // Once per frame whatever page shows (after `frame` when its page
        // does): for work driven by time alone, like a sequencer's clock.
        // May be null. Added after ABI 1 shipped: KARL checks `size`.
        void (*tick)(void *self, float dt);
        // MIDI clock, incoming or KARL's own Clock control, page showing or
        // not: 0 = Start, -1 = Stop, otherwise the 24 ppqn tick count since
        // the Start (Continue resumes the count). Ticks can arrive several to
        // a frame and only the last is passed, so derive the position from
        // the count rather than counting calls. May be null.
        void (*clock)(void *self, int tick);
    };

    // Live's transport, for a surface that subscribes to it (the "ableton"
    // plugin must be active):
    //   subscribe(host, "live:transport") -> event "live:transport",
    //     {"connected": bool, "playing": bool, "tempo": float, "beats": float}
    //     on connect, start, stop and tempo change. "beats" is the song
    //     position when it started.
    //   publish(host, "live:transport:get", "{}") asks for the current state.
    //   publish(host, "live:transport:set", "{\"playing\": true}") starts or
    //     stops Live.
    constexpr const char *LIVE_TRANSPORT = "live:transport";
    constexpr const char *LIVE_TRANSPORT_GET = "live:transport:get";
    constexpr const char *LIVE_TRANSPORT_SET = "live:transport:set";

    struct Plugin
    {
        uint32_t size;
        // KARL_SDK_ABI as built. KARL refuses any other.
        uint32_t abi;
        // Matches controls.json's "plugins".
        const char *name;
        const char *version;
        // Control type names, null-terminated.
        const char *const *types;
        const Surface *(*surface)(const char *type);
    };
}

// The one symbol KARL looks up. Its return value must stay valid forever.
KARL_PLUGIN_EXPORT const karl::sdk::Plugin *karl_plugin();
