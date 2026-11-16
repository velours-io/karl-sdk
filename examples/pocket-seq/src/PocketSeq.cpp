#include "PocketSeq.h"

#include <osg/BlendFunc>
#include <osg/Geode>
#include <osg/LineWidth>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

using namespace pocket;
using karl::sdk::Input;
using karl::sdk::KeyState;

namespace
{
    constexpr float PI = 3.14159265358979f;

    // The grid in the gap between the four knobs: 4x4 cells.
    constexpr float CELL = 18.0f;
    constexpr float GAP = 6.0f;
    constexpr float GRID = CELL * 4 + GAP * 3;

    // Rings as KARL draws its own: just outside the knob, 260 degrees from
    // the bottom left, clockwise.
    constexpr float RING_INNER = 49.0f;
    constexpr float RING_OUTER = 55.0f;
    constexpr float RING_START_DEG = 230.0f;
    constexpr float RING_SWEEP_DEG = 260.0f;
    constexpr int RING_SEGMENTS = 64;
    // The readout sits where KARL's box under a knob is.
    constexpr float READOUT_DROP = 72.5f;
    constexpr float READOUT_SIZE = 17.0f;
    // The panel behind the block, inset like KARL's encoder squares.
    constexpr float PANEL_INSET = 6.0f;
    constexpr float TITLE_SIZE = 13.0f;

    // Knob travel per step, in angle ticks (16384 a turn).
    constexpr float TICKS_SELECT = 1024.0f; // 16 steps a turn
    constexpr float TICKS_PITCH = 900.0f;   // about 20 degrees a semitone
    constexpr float TICKS_VELOCITY = 64.0f; // 0..127 in half a turn
    constexpr float TICKS_OCTAVE = 1400.0f;

    constexpr int MAX_OCTAVE = 7;
    constexpr float GATE = 0.5f;
    constexpr float PREVIEW_SECONDS = 0.15f;
    // MIDI clock: 24 ppqn, so six ticks to a 16th, and how long without one
    // before a paused host counts as stopped.
    constexpr int TICKS_PER_STEP = 6;
    constexpr float CLOCK_TIMEOUT = 0.5f;
    // Following Live: a start this far into a step (of 1) still plays it.
    constexpr float LATE_START = 0.25f;
    // Seconds after the last change before the pattern is written.
    constexpr float SAVE_DELAY = 1.0f;
    const osg::Vec4 AMBER(0.95f, 0.70f, 0.25f, 1.0f);

    const char *const NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    // C minor pentatonic, for ALT + STEP.
    const int PENTATONIC[] = {0, 3, 5, 7, 10};

    const osg::Vec4 OFF_CELL(0.17f, 0.18f, 0.21f, 1.0f);
    const osg::Vec4 DIM(0.27f, 0.28f, 0.32f, 1.0f);
    const osg::Vec4 TEXT(0.96f, 0.96f, 0.96f, 1.0f);
    const osg::Vec4 WHITE(1, 1, 1, 1);
    const osg::Vec4 GREEN(0.30f, 0.85f, 0.45f, 1.0f);
    const osg::Vec4 IDLE_KEY(0.56f, 0.56f, 0.58f, 1.0f);
    const osg::Vec4 PANEL(0.045f, 0.047f, 0.06f, 1.0f);
    const osg::Vec4 PANEL_EDGE(0.16f, 0.17f, 0.2f, 1.0f);
    const osg::Vec4 TITLE(0.55f, 0.56f, 0.6f, 1.0f);

    osg::Vec4 hsv(float h, float s, float v)
    {
        const float i = std::floor(h * 6.0f);
        const float f = h * 6.0f - i;
        const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
        switch (static_cast<int>(i) % 6)
        {
        case 0:
            return {v, t, p, 1};
        case 1:
            return {q, v, p, 1};
        case 2:
            return {p, v, t, 1};
        case 3:
            return {p, q, v, 1};
        case 4:
            return {t, p, v, 1};
        default:
            return {v, p, q, 1};
        }
    }

    osg::Vec4 pitchColor(int pitch, float brightness)
    {
        return hsv(pitch / 12.0f, 0.7f, brightness);
    }

    osg::Vec4 mix(const osg::Vec4 &a, const osg::Vec4 &b, float t)
    {
        return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
    }

    karl::sdk::Rgb rgb(const osg::Vec4 &c)
    {
        auto byte = [](float v)
        { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return {byte(c.r()), byte(c.g()), byte(c.b())};
    }

    bool pushed(const Input &input, int key)
    {
        return key >= 0 && key < karl::sdk::KEYS && input.key[key] == KeyState::PUSHED;
    }
}

int Knob::steps(int angle, float ticksPerStep)
{
    if (angle < 0)
    {
        return 0;
    }
    if (last < 0)
    {
        last = angle;
        return 0;
    }
    int delta = angle - last;
    last = angle;
    if (delta > karl::sdk::ANGLE_RESOLUTION / 2)
    {
        delta -= karl::sdk::ANGLE_RESOLUTION;
    }
    if (delta < -karl::sdk::ANGLE_RESOLUTION / 2)
    {
        delta += karl::sdk::ANGLE_RESOLUTION;
    }
    // Clockwise is a falling angle. Jitter on a knob nobody touches cancels
    // out in the sum instead of stepping.
    accumulated -= delta;
    const int whole = static_cast<int>(accumulated / ticksPerStep);
    accumulated -= whole * ticksPerStep;
    return whole;
}

PocketSeq::PocketSeq(const karl::sdk::Host *host, osg::Group *root, const Config &config)
    : _host(host), _config(config), _scene(new osg::Group), _sync(config.sync)
{
    root->addChild(_scene);
    osg::StateSet *state = _scene->getOrCreateStateSet();
    state->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    state->setMode(GL_BLEND, osg::StateAttribute::ON);
    state->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // The knobs under the surface, whichever they are: it is placed by its
    // x / y / width / height in controls.json, and must cover a 2x2 block.
    std::vector<Knob> found;
    for (int encoder = 0; encoder < karl::sdk::ENCODERS; encoder++)
    {
        Knob knob;
        if (!host->encoderCenter(host, encoder, &knob.x, &knob.y))
        {
            continue;
        }
        if (knob.x >= 0 && knob.y >= 0 && knob.x <= host->width && knob.y <= host->height)
        {
            knob.encoder = encoder;
            found.push_back(knob);
        }
    }
    if (found.size() != KNOBS)
    {
        host->log(host, "PocketSeq needs a surface over exactly 4 knobs in a 2x2 block (x, y, width, height).");
        return;
    }
    // Top row first, then left to right: select, pitch / velocity, octave.
    std::sort(found.begin(), found.end(), [](const Knob &a, const Knob &b)
              { return std::abs(a.y - b.y) > 1 ? a.y > b.y : a.x < b.x; });
    for (int role = 0; role < KNOBS; role++)
    {
        _knobs[role] = found[role];
        host->claimEncoder(host, found[role].encoder);
        _center += osg::Vec2(found[role].x, found[role].y) * 0.25f;
    }
    _ready = true;

    // "keys": {"play": [...], "step": [...]}, as the configurator places
    // them; the first two big keys of the bottom row otherwise.
    _playKey = karl::sdk::configKey(host, "play");
    _stepKey = karl::sdk::configKey(host, "step");
    if (_playKey < 0)
    {
        _playKey = host->keyIndex(host, karl::sdk::Edge::BOTTOM, 0);
    }
    if (_stepKey < 0)
    {
        _stepKey = host->keyIndex(host, karl::sdk::Edge::BOTTOM, 2);
    }
    host->claimKey(host, _playKey, 2);
    host->claimKey(host, _stepKey, 2);

    // Something to hear on the first PLAY.
    const int riff[STEPS][2] = {{0, 0}, {-1, 0}, {7, 0}, {-1, 0}, {3, 1}, {-1, 0}, {7, 0}, {10, 0},
                                {0, 0}, {-1, 0}, {5, 0}, {7, 0}, {-1, 0}, {3, 1}, {-1, 0}, {10, -1}};
    for (int i = 0; i < STEPS; i++)
    {
        _steps[i].on = riff[i][0] >= 0;
        _steps[i].pitch = std::max(0, riff[i][0]);
        _steps[i].octave = 3 + riff[i][1];
        _steps[i].velocity = i % 4 == 0 ? 112 : 84;
    }
    // The pattern as it was left, when there is one.
    load();

    host->subscribe(host, karl::sdk::LIVE_TRANSPORT);
    host->publish(host, karl::sdk::LIVE_TRANSPORT_GET, "{}");

    buildPanel();
    buildGrid();
    for (int role = 0; role < KNOBS; role++)
    {
        buildRing(static_cast<KnobRole>(role));
        osg::ref_ptr<osgText::Text> text = host->makeText(host, "", READOUT_SIZE, karl::sdk::Font::BOLD);
        text->setAlignment(osgText::Text::CENTER_CENTER);
        text->setPosition(osg::Vec3(_knobs[role].x, _knobs[role].y - READOUT_DROP, 0));
        _readouts[role] = text;
        _scene->addChild(text);
    }
    refresh();
}

PocketSeq::~PocketSeq()
{
    noteOff();
    if (_dirty)
    {
        save();
    }
}

// A dark panel over the whole surface, so the block reads as one instrument
// next to KARL's own squares, and its name and tempo along the top.
void PocketSeq::buildPanel()
{
    const float x0 = PANEL_INSET, y0 = PANEL_INSET;
    const float x1 = _host->width - PANEL_INSET, y1 = _host->height - PANEL_INSET;

    auto panel = new osg::Geometry;
    auto vertices = new osg::Vec3Array;
    for (const auto &v : {osg::Vec3(x0, y0, 0), osg::Vec3(x1, y0, 0), osg::Vec3(x1, y1, 0),
                          osg::Vec3(x0, y0, 0), osg::Vec3(x1, y1, 0), osg::Vec3(x0, y1, 0)})
    {
        vertices->push_back(v);
    }
    auto fill = new osg::Vec4Array;
    fill->push_back(PANEL);
    panel->setVertexArray(vertices);
    panel->setColorArray(fill, osg::Array::BIND_OVERALL);
    panel->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, vertices->size()));

    auto edge = new osg::Geometry;
    auto corners = new osg::Vec3Array;
    for (const auto &v : {osg::Vec3(x0, y0, 0), osg::Vec3(x1, y0, 0), osg::Vec3(x1, y1, 0), osg::Vec3(x0, y1, 0)})
    {
        corners->push_back(v);
    }
    auto edgeColor = new osg::Vec4Array;
    edgeColor->push_back(PANEL_EDGE);
    edge->setVertexArray(corners);
    edge->setColorArray(edgeColor, osg::Array::BIND_OVERALL);
    edge->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, 0, corners->size()));

    auto geode = new osg::Geode;
    geode->addDrawable(panel);
    geode->addDrawable(edge);
    _scene->addChild(geode);

    _title = _host->makeText(_host, "", TITLE_SIZE, karl::sdk::Font::BOLD);
    _title->setAlignment(osgText::Text::LEFT_CENTER);
    _title->setColor(TITLE);
    _title->setPosition(osg::Vec3(x0 + 12, y1 - 16, 0));
    _scene->addChild(_title);
}

void PocketSeq::buildGrid()
{
    auto geode = new osg::Geode;
    _cells = new osg::Geometry;
    _cells->setUseDisplayList(false);
    _cells->setDataVariance(osg::Object::DYNAMIC);
    auto vertices = new osg::Vec3Array;
    _cellColors = new osg::Vec4Array;
    const float left = _center.x() - GRID / 2;
    const float top = _center.y() + GRID / 2;
    for (int i = 0; i < STEPS; i++)
    {
        const float x0 = left + (i % 4) * (CELL + GAP);
        const float y1 = top - (i / 4) * (CELL + GAP);
        const float x1 = x0 + CELL, y0 = y1 - CELL;
        for (const auto &v : {osg::Vec3(x0, y0, 0), osg::Vec3(x1, y0, 0), osg::Vec3(x1, y1, 0),
                              osg::Vec3(x0, y0, 0), osg::Vec3(x1, y1, 0), osg::Vec3(x0, y1, 0)})
        {
            vertices->push_back(v);
            _cellColors->push_back(OFF_CELL);
        }
    }
    _cells->setVertexArray(vertices);
    _cells->setColorArray(_cellColors, osg::Array::BIND_PER_VERTEX);
    _cells->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, vertices->size()));
    geode->addDrawable(_cells);

    _cursor = new osg::Geometry;
    _cursor->setUseDisplayList(false);
    _cursor->setDataVariance(osg::Object::DYNAMIC);
    _cursorVertices = new osg::Vec3Array(4);
    auto cursorColor = new osg::Vec4Array;
    cursorColor->push_back(WHITE);
    _cursor->setVertexArray(_cursorVertices);
    _cursor->setColorArray(cursorColor, osg::Array::BIND_OVERALL);
    _cursor->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, 0, 4));
    _cursor->getOrCreateStateSet()->setAttributeAndModes(new osg::LineWidth(2.0f));
    geode->addDrawable(_cursor);
    _scene->addChild(geode);
}

void PocketSeq::buildRing(KnobRole role)
{
    auto ring = new osg::Geometry;
    ring->setUseDisplayList(false);
    ring->setDataVariance(osg::Object::DYNAMIC);
    ring->setVertexArray(new osg::Vec3Array);
    ring->setColorArray(new osg::Vec4Array, osg::Array::BIND_PER_VERTEX);
    ring->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_STRIP, 0, 0));
    ring->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_STRIP, 0, 0));
    auto geode = new osg::Geode;
    geode->addDrawable(ring);
    _scene->addChild(geode);
    _rings[role] = ring;
}

// The value part from the start of the sweep to `progress`, the rest dim.
void PocketSeq::refreshRing(KnobRole role, float progress, const osg::Vec4 &color)
{
    osg::Geometry *ring = _rings[role].get();
    auto *vertices = static_cast<osg::Vec3Array *>(ring->getVertexArray());
    auto *colors = static_cast<osg::Vec4Array *>(ring->getColorArray());
    vertices->clear();
    colors->clear();
    const osg::Vec3 center(_knobs[role].x, _knobs[role].y, 0);
    auto arc = [&](float from, float to, const osg::Vec4 &c)
    {
        const int segments = std::max(1, static_cast<int>(std::ceil((to - from) * RING_SEGMENTS)));
        for (int s = 0; s <= segments; s++)
        {
            const float t = from + (to - from) * s / segments;
            const float a = (RING_START_DEG - RING_SWEEP_DEG * t) * PI / 180.0f;
            const osg::Vec3 dir(std::cos(a), std::sin(a), 0);
            vertices->push_back(center + dir * RING_INNER);
            vertices->push_back(center + dir * RING_OUTER);
            colors->push_back(c);
            colors->push_back(c);
        }
    };
    progress = std::clamp(progress, 0.0f, 1.0f);
    arc(0.0f, std::max(progress, 0.004f), color);
    const size_t valueCount = vertices->size();
    arc(progress, 1.0f, DIM);
    static_cast<osg::DrawArrays *>(ring->getPrimitiveSet(0))->setCount(valueCount);
    auto *rest = static_cast<osg::DrawArrays *>(ring->getPrimitiveSet(1));
    rest->setFirst(valueCount);
    rest->setCount(vertices->size() - valueCount);
    vertices->dirty();
    colors->dirty();
    ring->dirtyBound();
}

int PocketSeq::noteOf(const Step &step) const
{
    return 12 * (step.octave + 1) + step.pitch;
}

void PocketSeq::frame(const Input &input, float)
{
    if (!_ready)
    {
        return;
    }
    handleKnobs(input);
    handleKeys(input);
    refresh();
}

void PocketSeq::handleKnobs(const Input &input)
{
    const float ticks[KNOBS] = {TICKS_SELECT, TICKS_PITCH, TICKS_VELOCITY, TICKS_OCTAVE};
    for (int role = 0; role < KNOBS; role++)
    {
        Knob &knob = _knobs[role];
        const int amount = knob.steps(input.angle[knob.encoder], ticks[role]);
        if (amount != 0)
        {
            editSelected(static_cast<KnobRole>(role), amount);
        }
    }
}

void PocketSeq::editSelected(KnobRole role, int amount)
{
    markDirty();
    if (role == SELECT)
    {
        _selected = ((_selected + amount) % STEPS + STEPS) % STEPS;
        return;
    }
    Step &step = _steps[_selected];
    // Touching a step's sound means you want to hear it.
    step.on = true;
    if (role == PITCH)
    {
        // Past B goes on to the next octave's C, and back.
        const int note = std::clamp(step.octave * 12 + step.pitch + amount, 0, MAX_OCTAVE * 12 + 11);
        step.octave = note / 12;
        step.pitch = note % 12;
        preview();
    }
    else if (role == OCTAVE)
    {
        step.octave = std::clamp(step.octave + amount, 0, MAX_OCTAVE);
        preview();
    }
    else
    {
        step.velocity = std::clamp(step.velocity + amount, 1, 127);
    }
}

// Plays the selected step briefly while nothing runs, so a pitch is chosen
// by ear.
void PocketSeq::preview()
{
    if (isRunning())
    {
        return;
    }
    noteOff();
    const Step &step = _steps[_selected];
    _sounding = noteOf(step);
    _host->sendNote(_host, _config.port, _config.channel, _sounding, step.velocity);
    _previewLeft = PREVIEW_SECONDS;
}

void PocketSeq::handleKeys(const Input &input)
{
    // Big keys: KARL reports either slot on the first.
    if (pushed(input, _playKey))
    {
        if (input.alt)
        {
            cycleSync();
        }
        else
        {
            pressPlay();
        }
    }

    auto state = [&](int key)
    { return key >= 0 ? input.key[key] : KeyState::IDLE; };
    const KeyState step = state(_stepKey);
    if (step == KeyState::PUSHED)
    {
        if (input.alt)
        {
            randomize();
            return;
        }
        _stepHeld = true;
        _stepLong = false;
    }
    if (_stepHeld && step == KeyState::LONG_PUSHED)
    {
        clear();
        _stepLong = true;
    }
    if (_stepHeld && step == KeyState::RELEASED)
    {
        if (!_stepLong)
        {
            _steps[_selected].on = !_steps[_selected].on;
            markDirty();
        }
        _stepHeld = false;
    }
}

void PocketSeq::randomize()
{
    std::random_device device;
    std::mt19937 random(device());
    std::uniform_int_distribution<int> degree(0, 4), coin(0, 99), octave(2, 4), velocity(70, 120);
    for (int i = 0; i < STEPS; i++)
    {
        Step &step = _steps[i];
        // Downbeats are likelier to play.
        step.on = coin(random) < (i % 4 == 0 ? 85 : 45);
        step.pitch = PENTATONIC[degree(random)];
        step.octave = octave(random);
        step.velocity = velocity(random);
    }
    markDirty();
}

void PocketSeq::clear()
{
    for (Step &step : _steps)
    {
        step.on = false;
    }
    markDirty();
}

void PocketSeq::pressPlay()
{
    if (_sync == Sync::LIVE)
    {
        // Live's transport is the one that runs: ask it, and follow what it
        // reports back.
        const std::string json = std::string("{\"playing\": ") + (_livePlaying ? "false" : "true") + "}";
        _host->publish(_host, karl::sdk::LIVE_TRANSPORT_SET, json.c_str());
        return;
    }
    _enabled = !_enabled;
    halt();
    // INTERNAL: the first tick fires step 1 at once. MIDI: the next clock
    // step does.
    _phase = 1.0f;
}

void PocketSeq::cycleSync()
{
    halt();
    _enabled = false;
    _sync = _sync == Sync::INTERNAL ? Sync::MIDI : _sync == Sync::MIDI ? Sync::LIVE : Sync::INTERNAL;
    if (_sync == Sync::LIVE)
    {
        _host->publish(_host, karl::sdk::LIVE_TRANSPORT_GET, "{}");
    }
    markDirty();
}

bool PocketSeq::isRunning() const
{
    switch (_sync)
    {
    case Sync::INTERNAL:
        return _enabled;
    case Sync::MIDI:
        return _enabled && _clockRunning;
    case Sync::LIVE:
        return _livePlaying;
    }
    return false;
}

void PocketSeq::halt()
{
    noteOff();
    _playhead = -1;
    _lastClockStep = -1;
}

void PocketSeq::noteOff()
{
    if (_sounding < 0)
    {
        return;
    }
    _host->sendNote(_host, _config.port, _config.channel, _sounding, 0);
    _sounding = -1;
}

void PocketSeq::fireStep(int index, float stepSeconds)
{
    noteOff();
    _playhead = index;
    _glow[_playhead] = 1.0f;
    if (_playhead % 4 == 0)
    {
        _beatFlash = 1.0f;
    }
    const Step &step = _steps[_playhead];
    if (step.on)
    {
        _sounding = noteOf(step);
        _host->sendNote(_host, _config.port, _config.channel, _sounding, step.velocity);
        _gateLeft = stepSeconds * GATE;
    }
}

void PocketSeq::advanceFree(float dt, float bpm)
{
    const float stepSeconds = 60.0f / std::max(20.0f, bpm) / 4.0f;
    _phase += dt / stepSeconds;
    while (_phase >= 1.0f)
    {
        _phase -= 1.0f;
        fireStep((_playhead + 1) % STEPS, stepSeconds);
    }
}

void PocketSeq::tick(float dt)
{
    if (!_ready)
    {
        return;
    }
    for (float &glow : _glow)
    {
        glow = std::max(0.0f, glow - dt * 3.0f);
    }
    _beatFlash = std::max(0.0f, _beatFlash - dt * 6.0f);

    if (_previewLeft > 0)
    {
        _previewLeft -= dt;
        if (_previewLeft <= 0)
        {
            noteOff();
        }
    }
    if (_sounding >= 0 && _gateLeft > 0)
    {
        _gateLeft -= dt;
        if (_gateLeft <= 0)
        {
            noteOff();
        }
    }
    if (_dirty)
    {
        _saveIn -= dt;
        if (_saveIn <= 0)
        {
            save();
        }
    }

    switch (_sync)
    {
    case Sync::INTERNAL:
        if (_enabled)
        {
            advanceFree(dt, _config.bpm);
        }
        break;
    case Sync::LIVE:
        if (_livePlaying)
        {
            advanceFree(dt, _liveTempo);
        }
        break;
    case Sync::MIDI:
        // A host that pauses sends no Stop: silence is the only sign.
        _sinceClockTick += dt;
        if (_clockRunning && _sinceClockTick > CLOCK_TIMEOUT)
        {
            _clockRunning = false;
            halt();
        }
        break;
    }
}

void PocketSeq::clock(int tick)
{
    if (!_ready || _sync != Sync::MIDI)
    {
        return;
    }
    if (tick == 0)
    {
        // Start: from the top, on the first tick that follows.
        _clockRunning = true;
        _lastClockTick = 0;
        _sinceClockTick = 0;
        halt();
        return;
    }
    if (tick < 0)
    {
        _clockRunning = false;
        halt();
        return;
    }
    // The tempo, from the time between deliveries over the ticks they span.
    if (_clockRunning && tick > _lastClockTick && _sinceClockTick > 0 && _sinceClockTick < CLOCK_TIMEOUT)
    {
        const float measured = _sinceClockTick / (tick - _lastClockTick);
        _clockTickSeconds += (measured - _clockTickSeconds) * 0.1f;
    }
    _clockRunning = true;
    _lastClockTick = tick;
    _sinceClockTick = 0;

    // Tick 1 is the downbeat. Six ticks of 24 ppqn are one 16th.
    const int step = (tick - 1) / TICKS_PER_STEP;
    if (step == _lastClockStep)
    {
        return;
    }
    _lastClockStep = step;
    if (_enabled)
    {
        fireStep(step % STEPS, _clockTickSeconds * TICKS_PER_STEP);
    }
}

void PocketSeq::event(const std::string &name, const std::string &json)
{
    if (name != karl::sdk::LIVE_TRANSPORT)
    {
        return;
    }
    const nlohmann::json data = nlohmann::json::parse(json, nullptr, false);
    if (!data.is_object())
    {
        return;
    }
    _liveConnected = data.value("connected", false);
    _liveTempo = data.value("tempo", _liveTempo);
    const bool playing = _liveConnected && data.value("playing", false);
    const bool started = playing && !_livePlaying;
    const bool stopped = !playing && _livePlaying;
    _livePlaying = playing;
    if (_sync != Sync::LIVE)
    {
        return;
    }
    if (stopped)
    {
        halt();
        return;
    }
    if (!started)
    {
        return;
    }
    // In phase with Live: where its song position falls in our 16 steps.
    // Close after a step boundary, that step plays now; later, wait for the
    // next one.
    const double position = data.value("beats", 0.0) * 4.0;
    const int whole = static_cast<int>(std::floor(position));
    const float fraction = static_cast<float>(position - whole);
    noteOff();
    if (fraction < LATE_START)
    {
        _playhead = ((whole - 1) % STEPS + STEPS) % STEPS;
        _phase = 1.0f;
    }
    else
    {
        _playhead = whole % STEPS;
        _phase = fraction;
    }
}

void PocketSeq::markDirty()
{
    _dirty = true;
    _saveIn = SAVE_DELAY;
}

std::string PocketSeq::storagePath() const
{
    return std::string(_host->dataDir(_host)) + _config.id + ".json";
}

void PocketSeq::load()
{
    std::ifstream file(storagePath());
    if (!file)
    {
        return;
    }
    const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
    if (!data.is_object() || !data.contains("steps") || !data["steps"].is_array())
    {
        _host->log(_host, ("PocketSeq: unreadable " + storagePath() + ", starting fresh").c_str());
        return;
    }
    const auto &steps = data["steps"];
    for (size_t i = 0; i < steps.size() && i < STEPS; i++)
    {
        if (!steps[i].is_object())
        {
            continue;
        }
        Step &step = _steps[i];
        step.on = steps[i].value("on", step.on);
        step.pitch = std::clamp(steps[i].value("pitch", step.pitch), 0, 11);
        step.velocity = std::clamp(steps[i].value("velocity", step.velocity), 1, 127);
        step.octave = std::clamp(steps[i].value("octave", step.octave), 0, MAX_OCTAVE);
    }
    _selected = std::clamp(data.value("selected", 0), 0, STEPS - 1);
    const std::string sync = data.value("sync", std::string());
    if (sync == "internal" || sync == "midi" || sync == "live")
    {
        _sync = sync == "midi" ? Sync::MIDI : sync == "live" ? Sync::LIVE : Sync::INTERNAL;
    }
}

// Written beside and renamed over, so a power cut mid-write leaves the last
// pattern rather than half of this one.
void PocketSeq::save()
{
    _dirty = false;
    nlohmann::json steps = nlohmann::json::array();
    for (const Step &step : _steps)
    {
        steps.push_back({{"on", step.on}, {"pitch", step.pitch}, {"velocity", step.velocity}, {"octave", step.octave}});
    }
    const char *sync = _sync == Sync::MIDI ? "midi" : _sync == Sync::LIVE ? "live" : "internal";
    const nlohmann::json data = {{"version", 1}, {"steps", steps}, {"selected", _selected}, {"sync", sync}};

    const std::string path = storagePath();
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file || !(file << data.dump(2)))
        {
            _host->log(_host, ("PocketSeq: could not write " + temporary).c_str());
            return;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        _host->log(_host, ("PocketSeq: could not save " + path + ": " + error.message()).c_str());
    }
}

void PocketSeq::refresh()
{
    for (int i = 0; i < STEPS; i++)
    {
        const Step &step = _steps[i];
        osg::Vec4 color = step.on ? pitchColor(step.pitch, 0.35f + 0.65f * step.velocity / 127.0f) : OFF_CELL;
        if (i == _playhead)
        {
            color = mix(color, WHITE, 0.25f);
        }
        color = mix(color, WHITE, _glow[i] * (step.on ? 0.8f : 0.3f));
        for (int v = 0; v < 6; v++)
        {
            (*_cellColors)[i * 6 + v] = color;
        }
    }
    _cellColors->dirty();

    const float left = _center.x() - GRID / 2 + (_selected % 4) * (CELL + GAP) - 3;
    const float top = _center.y() + GRID / 2 - (_selected / 4) * (CELL + GAP) + 3;
    const float size = CELL + 6;
    (*_cursorVertices)[0] = osg::Vec3(left, top - size, 0);
    (*_cursorVertices)[1] = osg::Vec3(left + size, top - size, 0);
    (*_cursorVertices)[2] = osg::Vec3(left + size, top, 0);
    (*_cursorVertices)[3] = osg::Vec3(left, top, 0);
    _cursorVertices->dirty();
    _cursor->dirtyBound();

    const Step &step = _steps[_selected];
    const osg::Vec4 sound = step.on ? pitchColor(step.pitch, 1.0f) : DIM;
    refreshRing(SELECT, _selected / float(STEPS - 1), WHITE);
    refreshRing(PITCH, step.pitch / 11.0f, sound);
    refreshRing(VELOCITY, step.velocity / 127.0f, sound);
    refreshRing(OCTAVE, step.octave / float(MAX_OCTAVE), sound);

    const std::string texts[KNOBS] = {
        "STEP " + std::to_string(_selected + 1),
        std::string(NOTE_NAMES[step.pitch]) + std::to_string(step.octave),
        "VEL " + std::to_string(step.velocity),
        "OCT " + std::to_string(step.octave),
    };
    for (int role = 0; role < KNOBS; role++)
    {
        if (_readouts[role]->getText().createUTF8EncodedString() != texts[role])
        {
            _readouts[role]->setText(texts[role], osgText::String::ENCODING_UTF8);
        }
        _readouts[role]->setColor(role == SELECT || step.on ? TEXT : DIM);
    }
    refreshTitle();
    refreshKeys();
}

// The source and its tempo along the top: the configured one, the one
// measured off the MIDI clock, or Live's.
void PocketSeq::refreshTitle()
{
    std::string source;
    switch (_sync)
    {
    case Sync::INTERNAL:
        source = std::to_string(static_cast<int>(std::lround(_config.bpm))) + " BPM";
        break;
    case Sync::MIDI:
        source = _clockRunning ? "MIDI CLOCK   " + std::to_string(static_cast<int>(std::lround(60.0f / (_clockTickSeconds * 24.0f)))) + " BPM"
                               : "MIDI CLOCK   NO SIGNAL";
        break;
    case Sync::LIVE:
        source = _liveConnected ? "LIVE   " + std::to_string(static_cast<int>(std::lround(_liveTempo))) + " BPM"
                                : "LIVE   NOT CONNECTED";
        break;
    }
    const std::string title = "POCKET SEQ   " + source;
    if (title != _shownTitle)
    {
        _shownTitle = title;
        _title->setText(title, osgText::String::ENCODING_UTF8);
    }
}

void PocketSeq::refreshKeys()
{
    // MIDI: armed but no clock yet waits, in amber. LIVE: what Live does.
    const bool waiting = _sync == Sync::MIDI && _enabled && !_clockRunning;
    std::string playLabel = isRunning() ? "STOP" : waiting ? "WAITING" : "PLAY";
    if (_sync == Sync::LIVE && !_liveConnected)
    {
        playLabel = "NO LIVE";
    }
    if (playLabel != _shownPlayLabel)
    {
        _shownPlayLabel = playLabel;
        _host->setLabel(_host, _playKey, playLabel.c_str());
    }
    // Green while running, flashing white on every beat.
    const osg::Vec4 playColor = isRunning() ? mix(GREEN, WHITE, _beatFlash) : waiting ? AMBER : IDLE_KEY;
    setKeyColor(_playKey, rgb(playColor), _shownPlayColor);

    const Step &step = _steps[_selected];
    const std::string stepLabel = "STEP " + std::to_string(_selected + 1) + (step.on ? " ON" : " OFF");
    if (stepLabel != _shownStepLabel)
    {
        _shownStepLabel = stepLabel;
        _host->setLabel(_host, _stepKey, stepLabel.c_str());
    }
    setKeyColor(_stepKey, rgb(step.on ? pitchColor(step.pitch, 1.0f) : IDLE_KEY), _shownStepColor);
}

// Only on a change: a key's color is also its LED.
void PocketSeq::setKeyColor(int key, karl::sdk::Rgb color, uint32_t &shown)
{
    const uint32_t packed = (color.r << 16) | (color.g << 8) | color.b;
    if (packed == shown)
    {
        return;
    }
    shown = packed;
    _host->setColor(_host, key, color);
}
