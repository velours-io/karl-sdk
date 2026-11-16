#pragma once

#include <karl/sdk.h>

#include <osg/Geometry>
#include <osg/Group>
#include <osgText/Text>

#include <array>
#include <string>

namespace pocket
{
    constexpr int STEPS = 16;

    struct Step
    {
        bool on = false;
        int pitch = 0; // 0..11, C..B
        int velocity = 100;
        int octave = 3; // note = 12 * (octave + 1) + pitch, so 3 is C3 = 48
    };

    // What drives the steps: KARL's own tempo, a MIDI clock (incoming, or
    // KARL's Clock control), or Live's transport.
    enum class Sync
    {
        INTERNAL,
        MIDI,
        LIVE,
    };

    struct Config
    {
        // Names the saved pattern, so two sequencers keep their own.
        std::string id = "pocket-seq";
        Sync sync = Sync::INTERNAL;
        float bpm = 120;
        int port = 0;
        int channel = 1;
    };

    // One knob read through its absolute angle: turning adds up ticks, and
    // every `ticksPerStep` of them is one step of whatever it sets.
    struct Knob
    {
        int encoder = -1;
        float x = 0, y = 0;
        int last = -1;
        float accumulated = 0;

        int steps(int angle, float ticksPerStep);
    };

    /**
     * A 16-step sequencer on a 2x2 block of knobs and two keys. Top left picks
     * the step, top right its pitch, bottom left its velocity, bottom right
     * its octave. The steps are a 4x4 grid in the gap between the four knobs,
     * coloured by pitch and lit by velocity; the playhead flashes as it passes.
     * Keys: PLAY / STOP, and the selected step on / off (ALT: a random
     * pentatonic pattern, hold: clear).
     */
    class PocketSeq
    {
    public:
        PocketSeq(const karl::sdk::Host *host, osg::Group *root, const Config &config);
        ~PocketSeq();

        void frame(const karl::sdk::Input &input, float dt);
        void tick(float dt);
        void clock(int tick);
        void event(const std::string &name, const std::string &json);

    private:
        enum KnobRole
        {
            SELECT,
            PITCH,
            VELOCITY,
            OCTAVE,
            KNOBS,
        };

        void buildPanel();
        void buildGrid();
        void buildRing(KnobRole role);
        void handleKnobs(const karl::sdk::Input &input);
        void handleKeys(const karl::sdk::Input &input);
        void editSelected(KnobRole role, int amount);
        void preview();
        void randomize();
        void clear();
        void pressPlay();
        void cycleSync();
        // Whether steps are going by right now, whatever drives them.
        bool isRunning() const;
        // Runs the steps off a tempo, for INTERNAL and LIVE.
        void advanceFree(float dt, float bpm);
        void fireStep(int index, float stepSeconds);
        // Silence and the playhead off: on stop, and on a change of sync.
        void halt();
        void noteOff();
        void markDirty();
        std::string storagePath() const;
        void load();
        void save();
        void refreshTitle();
        void refresh();
        void refreshKeys();
        void setKeyColor(int key, karl::sdk::Rgb color, uint32_t &shown);
        void refreshRing(KnobRole role, float progress, const osg::Vec4 &color);
        int noteOf(const Step &step) const;

        const karl::sdk::Host *_host;
        Config _config;
        osg::ref_ptr<osg::Group> _scene;

        std::array<Knob, KNOBS> _knobs;
        bool _ready = false;
        osg::Vec2 _center;

        std::array<Step, STEPS> _steps;
        int _selected = 0;

        Sync _sync;
        // PLAY in INTERNAL and MIDI: the sequence runs, or waits for the
        // clock to. In LIVE, Live's own transport decides.
        bool _enabled = false;
        // MIDI clock: running between a Start (or the first tick) and a Stop,
        // or half a second of silence from a host that paused.
        bool _clockRunning = false;
        int _lastClockTick = 0;
        int _lastClockStep = -1;
        float _sinceClockTick = 0;
        float _clockTickSeconds = 60.0f / 120.0f / 24.0f;
        // Live, from "live:transport".
        bool _liveConnected = false;
        bool _livePlaying = false;
        float _liveTempo = 120;

        // Saved a moment after the last change, and on the way out.
        bool _dirty = false;
        float _saveIn = 0;

        // Clock: _phase runs 0..1 over one step.
        int _playhead = -1;
        float _phase = 0;
        float _gateLeft = 0;
        int _sounding = -1;
        float _previewLeft = 0;
        // 1 when a step fires, fading: the playhead's flash.
        std::array<float, STEPS> _glow{};
        float _beatFlash = 0;

        int _playKey = -1;
        int _stepKey = -1;
        bool _stepHeld = false;
        bool _stepLong = false;

        osg::ref_ptr<osg::Geometry> _cells;
        osg::ref_ptr<osg::Vec4Array> _cellColors;
        osg::ref_ptr<osg::Geometry> _cursor;
        osg::ref_ptr<osg::Vec3Array> _cursorVertices;
        std::array<osg::ref_ptr<osg::Geometry>, KNOBS> _rings;
        std::array<osg::ref_ptr<osgText::Text>, KNOBS> _readouts;
        osg::ref_ptr<osgText::Text> _title;
        std::string _shownTitle;
        std::string _shownPlayLabel;
        std::string _shownStepLabel;
        uint32_t _shownPlayColor = 0xffffffff;
        uint32_t _shownStepColor = 0xffffffff;
    };
}
