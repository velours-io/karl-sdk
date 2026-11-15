#include "Tactical.h"

#include "View.h"

#include "Label.h"
#include <karl/traces.h>

#include <osg/BlendFunc>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Program>
#include <osg/Shader>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace fleet;

namespace
{
    constexpr float PI = 3.14159265f;

    // --- Look: everything worth tuning ---

    // The plot's colour, and the warmer one for dangerous contacts.
    const osg::Vec3 TACTICAL_CYAN(0.45f, 0.86f, 1.0f);
    const osg::Vec3 DANGER_AMBER(1.0f, 0.74f, 0.42f);
    // By day the cyan is darkened by this, to read on bright water.
    constexpr float DAY_SHADE = 0.4f;
    // Overall alpha scale by night and by day, and how much fog adds to it:
    // the instruments stay readable when the sea does not.
    constexpr float NIGHT_OPACITY = 0.8f;
    constexpr float DAY_OPACITY = 1.1f;
    constexpr float FOG_OPACITY_BOOST = 0.7f;

    // Intersection nodes: four corner ticks, this far off the copper and
    // this long. Alpha at rest.
    constexpr float NODE_GAP = 3.0f;
    constexpr float NODE_ARM = 5.0f;
    constexpr float GRID_OPACITY = 0.22f;
    constexpr float LINE_WIDTH = 1.0f;

    // Sector coordinates, in the cell's upper-left corner.
    constexpr float LABEL_SIZE = 9.0f;
    constexpr float LABEL_INSET_X = 10.0f;
    constexpr float LABEL_INSET_Y = 8.0f;
    constexpr float LABEL_OPACITY = 0.28f;
    constexpr float LABEL_ACTIVE_OPACITY = 0.75f;

    // Sector activity: corner brackets, a faint wash, the label brightened.
    constexpr float SECTOR_FLASH_SECONDS = 0.6f;
    constexpr float ACTIVE_OPACITY = 0.55f;
    constexpr float SECTOR_FILL_OPACITY = 0.035f;
    constexpr float SECTOR_BRACKET_ARM = 12.0f;
    constexpr float SECTOR_BRACKET_INSET = 4.0f;

    // Sonar rings: grow from the contact, fading out.
    constexpr float SONAR_START_RADIUS = 6.0f;
    constexpr float SONAR_MAX_RADIUS = 46.0f;
    constexpr float SONAR_SECONDS = 1.4f;
    constexpr float SONAR_OPACITY = 0.6f;
    constexpr int SONAR_SEGMENTS = 40;
    // Repeat pings: dangerous ships always, every ship once fog is past
    // FOG_PING_THRESHOLD. Each ping marks where the ship was, not where it is.
    constexpr float DANGER_PING_SECONDS = 3.0f;
    constexpr float FOG_PING_SECONDS = 4.0f;
    constexpr float FOG_PING_THRESHOLD = 0.5f;
    // Rings beyond this many are not started.
    constexpr size_t MAX_RINGS = 48;

    // A newly detected contact: a small circle pulsing round it.
    constexpr float CONTACT_PULSE_SECONDS = 1.2f;
    constexpr float CONTACT_PULSE_RADIUS = 11.0f;
    constexpr float CONTACT_PULSE_OPACITY = 0.6f;

    // Lock-on: brackets closing in on the target.
    constexpr float ACQUIRE_SECONDS = 0.9f;
    constexpr float ACQUIRE_FROM = 24.0f;
    constexpr float ACQUIRE_TO = 14.0f;
    constexpr float ACQUIRE_ARM = 5.0f;
    constexpr float ACQUIRE_OPACITY = 0.8f;

    // "CONTACT // B3", in the band at the foot of the screen, over the
    // bottom row of buttons, no more often than MESSAGE_MIN_GAP.
    constexpr float MESSAGE_SIZE = 10.0f;
    constexpr float MESSAGE_X = DISPLAY_WIDTH * 0.5f;
    constexpr float MESSAGE_Y = 46.0f;
    constexpr float MESSAGE_SECONDS = 1.8f;
    constexpr float MESSAGE_MIN_GAP = 1.2f;
    constexpr float MESSAGE_OPACITY = 0.7f;

    // --- Reading the grid off the traces ---

    // Parallel traces closer than this are one line.
    constexpr float TRACE_MERGE = 4.0f;
    // A grid line covers at least this much of the screen's height or width.
    constexpr float LINE_MIN_COVERAGE = 0.5f;
    // A cell side with less copper than this gets bars drawn in.
    constexpr float BORDER_MIN_COVERAGE = 0.6f;
    // The drawn-in bars: as dark as the copper shows through the panel.
    const osg::Vec4 BAR_COLOR(0.0f, 0.0f, 0.0f, 0.55f);
    constexpr float BAR_WIDTH = 1.0f;
    // How far along a line to look for an arm at a crossing.
    constexpr float ARM_PROBE = 12.0f;
    // Bands thinner than this along the top edge are not sectors.
    constexpr float MIN_SECTOR_SIZE = 100.0f;

    const char *VERTEX = R"(
#version 120
varying vec4 vColor;
void main()
{
    vColor = gl_Color;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

    const char *FRAGMENT = R"(
#version 120
uniform float uOpacity;
uniform float uShade;
varying vec4 vColor;
void main()
{
    gl_FragColor = vec4(vColor.rgb * uShade, min(1.0, vColor.a * uOpacity));
}
)";

    // An axis-aligned piece of copper: `at` across, from..to along.
    struct Segment
    {
        float at, from, to;
    };

    void collect(const karl::traces::TraceLine *lines, size_t count,
                 std::vector<Segment> &vertical, std::vector<Segment> &horizontal)
    {
        for (size_t i = 0; i < count; i++)
        {
            const karl::traces::TraceLine &l = lines[i];
            if (std::abs(l.x1 - l.x2) < 0.05f)
            {
                vertical.push_back({l.x1, std::min(l.y1, l.y2), std::max(l.y1, l.y2)});
            }
            else if (std::abs(l.y1 - l.y2) < 0.05f)
            {
                horizontal.push_back({l.y1, std::min(l.x1, l.x2), std::max(l.x1, l.x2)});
            }
        }
    }

    // How much of from..to is copper, along the line at `at`: 0..1.
    float coverage(const std::vector<Segment> &segments, float at, float from, float to)
    {
        std::vector<std::pair<float, float>> spans;
        for (const Segment &s : segments)
        {
            const float lo = std::max(s.from, from);
            const float hi = std::min(s.to, to);
            if (std::abs(s.at - at) <= TRACE_MERGE && hi > lo)
            {
                spans.emplace_back(lo, hi);
            }
        }
        std::sort(spans.begin(), spans.end());
        float covered = 0;
        float end = from;
        for (const auto &[lo, hi] : spans)
        {
            covered += std::max(0.0f, hi - std::max(lo, end));
            end = std::max(end, hi);
        }
        return covered / (to - from);
    }

    // Where the long lines are: parallel traces merged, short stubs dropped.
    std::vector<float> gridLines(std::vector<Segment> segments, float extent)
    {
        std::sort(segments.begin(), segments.end(), [](const Segment &a, const Segment &b)
                  { return a.at < b.at; });
        std::vector<float> lines;
        size_t i = 0;
        while (i < segments.size())
        {
            float weighted = 0;
            float length = 0;
            size_t j = i;
            for (; j < segments.size() && segments[j].at - segments[i].at <= TRACE_MERGE; j++)
            {
                const float l = segments[j].to - segments[j].from;
                weighted += segments[j].at * l;
                length += l;
            }
            const float at = weighted / std::max(length, 1e-3f);
            const bool repeat = !lines.empty() && at - lines.back() < 2 * TRACE_MERGE;
            if (!repeat && coverage(segments, at, 0, extent) >= LINE_MIN_COVERAGE)
            {
                lines.push_back(at);
            }
            i = j;
        }
        return lines;
    }

    osg::Vec4 withAlpha(const osg::Vec3 &rgb, float alpha)
    {
        return osg::Vec4(rgb, alpha);
    }

    osg::ref_ptr<osg::Geometry> lineGeometry(osg::Vec3Array *vertices, osg::Vec4Array *colors, osg::DrawArrays *draw)
    {
        osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry();
        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);
        geometry->setVertexArray(vertices);
        geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        geometry->addPrimitiveSet(draw);
        return geometry;
    }
}

Tactical::Tactical(const karl::sdk::Host *host)
{
    _opacityUniform = new osg::Uniform("uOpacity", _opacity);
    _shadeUniform = new osg::Uniform("uShade", _shade);

    osg::ref_ptr<osg::Program> program = new osg::Program();
    program->addShader(new osg::Shader(osg::Shader::VERTEX, VERTEX));
    program->addShader(new osg::Shader(osg::Shader::FRAGMENT, FRAGMENT));

    osg::ref_ptr<osg::Geode> geode = new osg::Geode();
    osg::StateSet *state = geode->getOrCreateStateSet();
    state->setAttributeAndModes(program, osg::StateAttribute::ON);
    state->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    state->setMode(GL_BLEND, osg::StateAttribute::ON);
    state->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    state->setAttributeAndModes(new osg::LineWidth(LINE_WIDTH));
    state->setRenderBinDetails(BIN_TACTICAL, "RenderBin");
    state->addUniform(_opacityUniform);
    state->addUniform(_shadeUniform);
    geode->setCullingActive(false);
    this->addChild(geode);

    _fillVertices = new osg::Vec3Array();
    _fillColors = new osg::Vec4Array();
    _fillDraw = new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 0);
    _fills = lineGeometry(_fillVertices, _fillColors, _fillDraw);
    _fills->setDataVariance(osg::Object::DYNAMIC);
    _fills->setCullingActive(false);

    _lineVertices = new osg::Vec3Array();
    _lineColors = new osg::Vec4Array();
    _lineDraw = new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, 0);
    _lines = lineGeometry(_lineVertices, _lineColors, _lineDraw);
    _lines->setDataVariance(osg::Object::DYNAMIC);
    _lines->setCullingActive(false);

    _contacts.reserve(64);
    _rings.reserve(MAX_RINGS);
    _lineVertices->reserve(8192);
    _lineColors->reserve(8192);

    // Washes under the static nodes, moving marks over them.
    geode->addDrawable(_fills);
    if (osg::ref_ptr<osg::Geometry> nodes = buildGrid(host))
    {
        geode->addDrawable(nodes);
    }
    geode->addDrawable(_lines);

    _message = new Label(host, "");
    _message->setCharacterSize(MESSAGE_SIZE);
    _message->setAlignment(osgText::Text::CENTER_CENTER);
    _message->setPos(MESSAGE_X, MESSAGE_Y);
    _message->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    _message->getOrCreateStateSet()->setRenderBinDetails(BIN_TACTICAL + 1, "RenderBin");
    _message->setColor(_messageColor);
    this->addChild(_message);
}

osg::ref_ptr<osg::Geometry> Tactical::buildGrid(const karl::sdk::Host *host)
{
    std::vector<Segment> vertical, horizontal;
    collect(karl::traces::F_CU_LINES, karl::traces::F_CU_COUNT, vertical, horizontal);
    collect(karl::traces::B_CU_LINES, karl::traces::B_CU_COUNT, vertical, horizontal);
    const std::vector<float> xs = gridLines(vertical, DISPLAY_HEIGHT);
    const std::vector<float> ys = gridLines(horizontal, DISPLAY_WIDTH);
    if (xs.size() < 2)
    {
        return nullptr;
    }

    // Where a band has no copper down a column line, draw the line in: the
    // same parallel traces the other bands have there, so every band splits
    // into the same columns.
    osg::ref_ptr<osg::Vec3Array> barVertices = new osg::Vec3Array();
    auto drawBars = [&](float x, float y0, float y1)
    {
        std::vector<float> offsets;
        for (const Segment &seg : vertical)
        {
            if (std::abs(seg.at - x) <= TRACE_MERGE && seg.to - seg.from > MIN_SECTOR_SIZE * 0.5f)
            {
                offsets.push_back(seg.at);
            }
        }
        std::sort(offsets.begin(), offsets.end());
        float last = -1e6f;
        for (float at : offsets)
        {
            if (at - last < 0.5f)
            {
                continue;
            }
            last = at;
            barVertices->push_back(osg::Vec3(at, y0, 0));
            barVertices->push_back(osg::Vec3(at, y1, 0));
            vertical.push_back({at, y0, y1});
        }
    };

    // Rows: the bands between horizontal lines and along the top edge, when
    // tall enough. Lettered from the top, numbered by column. The band along
    // the bottom edge is the HUD's, not the plot's.
    std::vector<float> bands(ys.begin(), ys.end());
    bands.push_back(DISPLAY_HEIGHT);
    char letter = 'A';
    for (int b = static_cast<int>(bands.size()) - 2; b >= 0; b--)
    {
        const float y0 = bands[b];
        const float y1 = bands[b + 1];
        if (y1 - y0 < MIN_SECTOR_SIZE)
        {
            continue;
        }
        for (size_t c = 1; c + 1 < xs.size(); c++)
        {
            if (coverage(vertical, xs[c], y0, y1) < BORDER_MIN_COVERAGE)
            {
                drawBars(xs[c], y0, y1);
            }
        }
        for (size_t c = 0; c + 1 < xs.size(); c++)
        {
            Sector sector;
            sector.x0 = xs[c];
            sector.x1 = xs[c + 1];
            sector.y0 = y0;
            sector.y1 = y1;
            sector.name = std::string(1, letter) + std::to_string(c + 1);
            sector.label = new Label(host, sector.name);
            sector.label->setCharacterSize(LABEL_SIZE);
            sector.label->setAlignment(osgText::Text::LEFT_TOP);
            sector.label->setPos(sector.x0 + LABEL_INSET_X, sector.y1 - LABEL_INSET_Y);
            osg::StateSet *state = sector.label->getOrCreateStateSet();
            state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            state->setRenderBinDetails(BIN_TACTICAL + 1, "RenderBin");
            this->addChild(sector.label);
            _sectors.push_back(sector);
        }
        letter++;
    }

    if (!barVertices->empty())
    {
        osg::ref_ptr<osg::Vec4Array> barColors = new osg::Vec4Array();
        barColors->push_back(BAR_COLOR);
        osg::ref_ptr<osg::Geometry> bars = new osg::Geometry();
        bars->setVertexArray(barVertices);
        bars->setColorArray(barColors, osg::Array::BIND_OVERALL);
        bars->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, barVertices->size()));
        // Copper, not instrument: not dimmed or boosted with the plot.
        osg::ref_ptr<osg::Geode> geode = new osg::Geode();
        geode->addDrawable(bars);
        osg::StateSet *state = geode->getOrCreateStateSet();
        state->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        state->setMode(GL_BLEND, osg::StateAttribute::ON);
        state->setAttributeAndModes(new osg::LineWidth(BAR_WIDTH));
        state->setRenderBinDetails(BIN_TACTICAL - 1, "RenderBin");
        this->addChild(geode);
    }

    // Intersection nodes: a corner tick in each quadrant two arms enclose,
    // so a T-junction gets two and a full crossing four. Only inside the
    // plot: none along the bottom band.
    osg::ref_ptr<osg::Vec3Array> nodeVertices = new osg::Vec3Array();
    for (float x : xs)
    {
        for (float y : ys)
        {
            const bool north = coverage(vertical, x, y, y + ARM_PROBE) > 0.8f;
            const bool south = coverage(vertical, x, y - ARM_PROBE, y) > 0.8f;
            const bool east = coverage(horizontal, y, x, x + ARM_PROBE) > 0.8f;
            const bool west = coverage(horizontal, y, x - ARM_PROBE, x) > 0.8f;
            const struct
            {
                bool on;
                float sx, sy;
            } quadrants[] = {{north && east, 1, 1}, {north && west, -1, 1}, {south && east, 1, -1}, {south && west, -1, -1}};
            for (const auto &q : quadrants)
            {
                const osg::Vec3 corner(x + q.sx * NODE_GAP, y + q.sy * NODE_GAP, 0);
                if (!q.on || sectorAt(osg::Vec2(corner.x(), corner.y())) < 0)
                {
                    continue;
                }
                nodeVertices->push_back(corner);
                nodeVertices->push_back(corner + osg::Vec3(q.sx * NODE_ARM, 0, 0));
                nodeVertices->push_back(corner);
                nodeVertices->push_back(corner + osg::Vec3(0, q.sy * NODE_ARM, 0));
            }
        }
    }
    osg::ref_ptr<osg::Vec4Array> nodeColors = new osg::Vec4Array();
    nodeColors->assign(nodeVertices->size(), withAlpha(TACTICAL_CYAN, GRID_OPACITY));
    return lineGeometry(nodeVertices, nodeColors,
                        new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, nodeVertices->size()));
}

void Tactical::setConditions(float day, float fog)
{
    _shade = 1.0f + (DAY_SHADE - 1.0f) * day;
    _opacity = (NIGHT_OPACITY + (DAY_OPACITY - NIGHT_OPACITY) * day) * (1.0f + FOG_OPACITY_BOOST * fog);
    _opacityUniform->set(_opacity);
    _shadeUniform->set(_shade);
    // Fog switches on the sweep: every contact pinged now and then.
    _foggy = fog > FOG_PING_THRESHOLD;
}

int Tactical::sectorAt(const osg::Vec2 &pos) const
{
    for (size_t i = 0; i < _sectors.size(); i++)
    {
        const Sector &s = _sectors[i];
        if (pos.x() >= s.x0 && pos.x() < s.x1 && pos.y() >= s.y0 && pos.y() < s.y1)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Tactical::contact(int id, const osg::Vec2 &pos, bool dangerous)
{
    auto it = std::find_if(_contacts.begin(), _contacts.end(), [id](const Contact &c)
                           { return c.id == id; });
    if (it == _contacts.end())
    {
        Contact fresh;
        fresh.id = id;
        _contacts.push_back(fresh);
        it = _contacts.end() - 1;
    }
    Contact &c = *it;
    c.pos = pos;
    c.dangerous = dangerous;
    c.seen = true;

    const int sector = sectorAt(pos);
    if (sector == c.sector)
    {
        return;
    }
    c.sector = sector;
    if (sector < 0)
    {
        return;
    }
    if (!c.detected)
    {
        detect(c);
        return;
    }
    enterSector(sector, "TRACK");
}

void Tactical::acquire(int id)
{
    for (Contact &c : _contacts)
    {
        if (c.id == id)
        {
            c.acquireAge = 0;
        }
    }
}

void Tactical::detect(Contact &contact)
{
    contact.detected = true;
    contact.pulseAge = 0;
    // The first repeat staggered, so a fleet's pings do not all go out together.
    const float stagger = std::fmod(contact.id * 0.618f, 1.0f);
    contact.pingTimer = (contact.dangerous ? DANGER_PING_SECONDS : FOG_PING_SECONDS) * (0.5f + 0.5f * stagger);
    ping(contact.pos, contact.dangerous);
    enterSector(contact.sector, "CONTACT");
}

void Tactical::enterSector(int sector, const char *what)
{
    _sectors[sector].flash = 1.0f;
    if (_messageAge >= MESSAGE_MIN_GAP)
    {
        showMessage(std::string(what) + " // " + _sectors[sector].name);
    }
}

void Tactical::ping(const osg::Vec2 &at, bool danger)
{
    if (_rings.size() >= MAX_RINGS)
    {
        return;
    }
    Ring ring;
    ring.at = at;
    ring.danger = danger;
    _rings.push_back(ring);
}

void Tactical::showMessage(const std::string &text)
{
    _message->setText(text);
    _messageAge = 0;
}

void Tactical::line(const osg::Vec2 &a, const osg::Vec2 &b, const osg::Vec4 &color)
{
    _lineVertices->push_back(osg::Vec3(a, 0));
    _lineVertices->push_back(osg::Vec3(b, 0));
    _lineColors->push_back(color);
    _lineColors->push_back(color);
}

void Tactical::circle(const osg::Vec2 &center, float radius, const osg::Vec4 &color)
{
    osg::Vec2 previous = center + osg::Vec2(radius, 0);
    for (int i = 1; i <= SONAR_SEGMENTS; i++)
    {
        const float a = 2 * PI * i / SONAR_SEGMENTS;
        const osg::Vec2 next = center + osg::Vec2(std::cos(a), std::sin(a)) * radius;
        line(previous, next, color);
        previous = next;
    }
}

void Tactical::brackets(float x0, float y0, float x1, float y1, float arm, const osg::Vec4 &color)
{
    for (const auto &[x, sx] : {std::pair{x0, 1.0f}, std::pair{x1, -1.0f}})
    {
        for (const auto &[y, sy] : {std::pair{y0, 1.0f}, std::pair{y1, -1.0f}})
        {
            line(osg::Vec2(x, y), osg::Vec2(x + sx * arm, y), color);
            line(osg::Vec2(x, y), osg::Vec2(x, y + sy * arm), color);
        }
    }
}

void Tactical::fill(const Sector &sector, const osg::Vec4 &color)
{
    _fillVertices->push_back(osg::Vec3(sector.x0, sector.y0, 0));
    _fillVertices->push_back(osg::Vec3(sector.x1, sector.y0, 0));
    _fillVertices->push_back(osg::Vec3(sector.x1, sector.y1, 0));
    _fillVertices->push_back(osg::Vec3(sector.x0, sector.y1, 0));
    for (int i = 0; i < 4; i++)
    {
        _fillColors->push_back(color);
    }
}

void Tactical::step(float dt)
{
    // Whatever was not reported since the last step has sunk or sailed off.
    _contacts.erase(std::remove_if(_contacts.begin(), _contacts.end(), [](const Contact &c)
                                   { return !c.seen; }),
                    _contacts.end());

    for (Contact &c : _contacts)
    {
        c.seen = false;
        if (c.pulseAge >= 0)
        {
            c.pulseAge = c.pulseAge + dt < CONTACT_PULSE_SECONDS ? c.pulseAge + dt : -1;
        }
        if (c.acquireAge >= 0)
        {
            c.acquireAge = c.acquireAge + dt < ACQUIRE_SECONDS ? c.acquireAge + dt : -1;
        }
        const float interval = c.dangerous ? DANGER_PING_SECONDS : _foggy ? FOG_PING_SECONDS : 0;
        if (!c.detected || interval <= 0)
        {
            continue;
        }
        c.pingTimer -= dt;
        if (c.pingTimer <= 0)
        {
            c.pingTimer = interval;
            ping(c.pos, c.dangerous);
        }
    }
    for (Ring &ring : _rings)
    {
        ring.age += dt;
    }
    _rings.erase(std::remove_if(_rings.begin(), _rings.end(), [](const Ring &r)
                                { return r.age >= SONAR_SECONDS; }),
                 _rings.end());

    _lineVertices->clear();
    _lineColors->clear();
    _fillVertices->clear();
    _fillColors->clear();

    for (Sector &s : _sectors)
    {
        s.flash = std::max(0.0f, s.flash - dt / SECTOR_FLASH_SECONDS);
        if (s.flash > 0)
        {
            fill(s, withAlpha(TACTICAL_CYAN, SECTOR_FILL_OPACITY * s.flash));
            brackets(s.x0 + SECTOR_BRACKET_INSET, s.y0 + SECTOR_BRACKET_INSET,
                     s.x1 - SECTOR_BRACKET_INSET, s.y1 - SECTOR_BRACKET_INSET,
                     SECTOR_BRACKET_ARM, withAlpha(TACTICAL_CYAN, ACTIVE_OPACITY * s.flash));
        }
        setTextAlpha(*s.label, s.labelColor, LABEL_OPACITY + (LABEL_ACTIVE_OPACITY - LABEL_OPACITY) * s.flash);
    }

    for (const Ring &ring : _rings)
    {
        const float t = ring.age / SONAR_SECONDS;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        const float radius = SONAR_START_RADIUS + (SONAR_MAX_RADIUS - SONAR_START_RADIUS) * eased;
        const osg::Vec3 &rgb = ring.danger ? DANGER_AMBER : TACTICAL_CYAN;
        circle(ring.at, radius, withAlpha(rgb, SONAR_OPACITY * std::pow(1.0f - t, 1.5f)));
        // The fix itself: a small cross, gone by half way.
        const float cross = std::max(0.0f, 1.0f - t * 2.0f);
        if (cross > 0)
        {
            const osg::Vec4 color = withAlpha(rgb, SONAR_OPACITY * cross);
            line(ring.at - osg::Vec2(4, 0), ring.at + osg::Vec2(4, 0), color);
            line(ring.at - osg::Vec2(0, 4), ring.at + osg::Vec2(0, 4), color);
        }
    }

    for (const Contact &c : _contacts)
    {
        const osg::Vec3 &rgb = c.dangerous ? DANGER_AMBER : TACTICAL_CYAN;
        if (c.pulseAge >= 0)
        {
            const float t = c.pulseAge / CONTACT_PULSE_SECONDS;
            // Two beats, fading.
            const float beat = 0.5f + 0.5f * std::cos(t * 4 * PI);
            circle(c.pos, CONTACT_PULSE_RADIUS, withAlpha(rgb, CONTACT_PULSE_OPACITY * beat * (1 - t)));
        }
        if (c.acquireAge >= 0)
        {
            const float t = c.acquireAge / ACQUIRE_SECONDS;
            const float close = std::min(1.0f, t / 0.35f);
            const float half = ACQUIRE_FROM + (ACQUIRE_TO - ACQUIRE_FROM) * (1 - (1 - close) * (1 - close));
            const float alpha = ACQUIRE_OPACITY * (t < 0.6f ? 1.0f : (1 - t) / 0.4f);
            brackets(c.pos.x() - half, c.pos.y() - half, c.pos.x() + half, c.pos.y() + half,
                     ACQUIRE_ARM, withAlpha(rgb, alpha));
        }
    }

    _lineDraw->setCount(static_cast<GLsizei>(_lineVertices->size()));
    _fillDraw->setCount(static_cast<GLsizei>(_fillVertices->size()));
    _lineVertices->dirty();
    _lineColors->dirty();
    _fillVertices->dirty();
    _fillColors->dirty();
    _lines->dirtyBound();
    _fills->dirtyBound();

    _messageAge += dt;
    const float t = _messageAge / MESSAGE_SECONDS;
    setTextAlpha(*_message, _messageColor, t >= 1 ? 0 : MESSAGE_OPACITY * (t < 0.7f ? 1.0f : (1 - t) / 0.3f));
}

void Tactical::setTextAlpha(Label &text, osg::Vec4 &written, float alpha)
{
    const osg::Vec4 color(TACTICAL_CYAN * _shade, std::min(1.0f, alpha * _opacity));
    if ((color - written).length2() < 1e-5f)
    {
        return;
    }
    written = color;
    text.setColor(color);
}
