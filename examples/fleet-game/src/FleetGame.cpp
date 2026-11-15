#include "FleetGame.h"

#include "Battle.h"
#include "View.h"
#include "Water.h"

#include <algorithm>
#include <cmath>
#include <string>

using namespace fleet;
using karl::sdk::Edge;
using karl::sdk::Input;
using karl::sdk::KeyState;
using karl::sdk::Rgb;

namespace
{
    // Sign that makes the beam follow the knob's turn, checked on the panel.
    constexpr float ANGLE_SIGN = 1.0f;
    constexpr float TWO_PI = 6.28318530718f;

    // How foggy FOG is: the shader's uFogAmount, 0 being clear.
    constexpr float FOG_AMOUNT = 0.675f;
    // Seconds DAY / NIGHT and FOG / CLEAR take to fade across.
    constexpr float FADE_SECONDS = 1.5f;
    // The longest frame a step may take, so a stall (a page switch, a
    // debugger) does not teleport ships.
    constexpr float MAX_DT = 0.05f;

    // What the LIGHTS button steps through: how many lighthouses may be lit
    // at once.
    const int LIGHT_LIMITS[] = {1, 2, 3, 5, 8, 12, 24};
    constexpr int DEFAULT_LIGHT_LIMIT = 3; // index of 5

    // What the SPEED button steps through, as multiples of the ships' speed.
    const float SPEEDS[] = {0.25f, 0.4f, 0.6f, 1.0f};
    constexpr int DEFAULT_SPEED = 1;

    constexpr Rgb IDLE_COLOR{0x8e, 0x8e, 0x93};
    constexpr Rgb ACTIVE_COLOR{0xff, 0xcc, 0x66};

    // How far out a shot's marker moves per radian of ALT + turn: the whole
    // range in about a third of a turn.
    constexpr float CHARGE_PER_RADIAN = 120.0f;
    // How far a knob must turn, with ALT held, before it starts a shot - and,
    // with or without ALT, before it lights its lighthouse.
    constexpr float START_TURN = 8.0f * TWO_PI / 360.0f;
    // Seconds over which a knob's small movements are added up for that.
    constexpr float ACTIVITY_MEMORY = 1.0f;

    // `value` moved towards `target` by at most `step`.
    float approach(float value, float target, float step)
    {
        return value < target ? std::min(target, value + step) : std::max(target, value - step);
    }
}

FleetGame::FleetGame(const karl::sdk::Host *host, osg::Group *root, const std::string &mode)
    : _host(host), _scene(new osg::Group), _lightLimit(DEFAULT_LIGHT_LIMIT), _speed(DEFAULT_SPEED)
{
    root->addChild(_scene);
    // The sea covers the panel; the empty encoder squares would float over it.
    host->showEncoderBackground(host, false);

    _water = new Water(host->width, host->height);
    _scene->addChild(_water);
    const bool canal = mode == "canal";
    _battle = new Battle(host, _water->beamTexture(), canal ? Battle::Mode::CANAL : Battle::Mode::SEA);
    _scene->getOrCreateStateSet()->addUniform(new osg::Uniform(
        "uWater", canal ? osg::Vec4(CANAL_X0, CANAL_Y0, CANAL_X1, CANAL_Y1)
                        : osg::Vec4(-1e5f, -1e5f, 1e5f, 1e5f)));
    _scene->addChild(_battle);

    // Read by every shader under here, the beam pass included.
    _fogAmount = _fog ? FOG_AMOUNT : 0.0f;
    _dayUniform = new osg::Uniform("uDay", _dayAmount);
    _fogUniform = new osg::Uniform("uFogAmount", _fogAmount);
    _scene->getOrCreateStateSet()->addUniform(_dayUniform);
    _scene->getOrCreateStateSet()->addUniform(_fogUniform);

    _dayKey = claim(Edge::RIGHT, 0, 1, "DAY");
    _nightKey = claim(Edge::RIGHT, 1, 1, "NIGHT");
    _fogKey = claim(Edge::RIGHT, 3, 1, "FOG");
    _clearKey = claim(Edge::RIGHT, 4, 1, "CLEAR");
    _speedKey = claim(Edge::RIGHT, 5, 1, "");
    _lightsKey = claim(Edge::RIGHT, 6, 1, "");
    _autoFireKey = claim(Edge::RIGHT, 7, 1, "");
    _sideKeys = {_dayKey, _nightKey, _fogKey, _clearKey, _speedKey, _lightsKey, _autoFireKey};

    // Bottom left: send the next wave. Pressed again, another comes alongside.
    _waveKey = claim(Edge::BOTTOM, 0, 2, "SEND WAVE");
    _waveKey2 = host->keyIndex(host, Edge::BOTTOM, 1);
    light(_waveKey, true);
    // The rest of the bottom row only shows the game's numbers: wave, ships
    // on the map, lives and ships through, score.
    _waveInfoKey = claim(Edge::BOTTOM, 2, 2, "");
    _shipsKey = claim(Edge::BOTTOM, 4, 2, "");
    _livesKey = claim(Edge::BOTTOM, 6, 2, "");
    _scoreKey = claim(Edge::BOTTOM, 8, 2, "");
    refreshHud();

    _battle->setLightLimit(LIGHT_LIMITS[_lightLimit]);
    _battle->setAutoFire(_autoFire);
    _battle->setSpeed(SPEEDS[_speed]);
    refreshSideButtons();

    // One lighthouse over each knob, at the middle of its cell.
    for (int encoder = 0; encoder < karl::sdk::ENCODERS; encoder++)
    {
        float x = 0, y = 0;
        if (!host->encoderCenter(host, encoder, &x, &y))
        {
            continue;
        }
        host->claimEncoder(host, encoder);
        _water->setLighthouse(encoder, x, y);
        _battle->placeTower(encoder, x, y);
    }
}

FleetGame::~FleetGame() = default;

int FleetGame::claim(Edge edge, int slot, int span, const char *text)
{
    const int key = _host->keyIndex(_host, edge, slot);
    _host->claimKey(_host, key, span);
    _host->setLabel(_host, key, text);
    _host->setColor(_host, key, IDLE_COLOR);
    return key;
}

void FleetGame::light(int key, bool active)
{
    _host->setColor(_host, key, active ? ACTIVE_COLOR : IDLE_COLOR);
}

void FleetGame::label(int key, const std::string &text)
{
    _host->setLabel(_host, key, text.c_str());
}

bool FleetGame::pushed(const Input &input, int key)
{
    return key >= 0 && key < karl::sdk::KEYS && input.key[key] == KeyState::PUSHED;
}

void FleetGame::setDay(bool day)
{
    _day = day;
    _battle->setDay(day);
    refreshSideButtons();
}

void FleetGame::setFog(bool fog)
{
    _fog = fog;
    refreshSideButtons();
}

void FleetGame::nextSpeed()
{
    _speed = (_speed + 1) % static_cast<int>(std::size(SPEEDS));
    _battle->setSpeed(SPEEDS[_speed]);
    refreshSideButtons();
}

void FleetGame::nextLightLimit()
{
    _lightLimit = (_lightLimit + 1) % static_cast<int>(std::size(LIGHT_LIMITS));
    _battle->setLightLimit(LIGHT_LIMITS[_lightLimit]);
    refreshSideButtons();
}

void FleetGame::toggleAutoFire()
{
    _autoFire = !_autoFire;
    _battle->setAutoFire(_autoFire);
    refreshSideButtons();
}

void FleetGame::refreshSideButtons()
{
    light(_dayKey, _day);
    light(_nightKey, !_day);
    light(_fogKey, _fog);
    light(_clearKey, !_fog);
    light(_autoFireKey, _autoFire);
    label(_autoFireKey, _autoFire ? "Auto fire ON" : "Auto fire OFF");
    label(_lightsKey, "LIGHTS " + std::to_string(LIGHT_LIMITS[_lightLimit]));
    label(_speedKey, "SPEED " + std::to_string(_speed + 1));
}

void FleetGame::refreshHud()
{
    if (_battle->wave() != _shownWave)
    {
        _shownWave = _battle->wave();
        label(_waveInfoKey, "WAVE " + std::to_string(_shownWave));
    }
    if (_battle->shipsAfloat() != _shownShips)
    {
        _shownShips = _battle->shipsAfloat();
        label(_shipsKey, std::to_string(_shownShips) + (_shownShips == 1 ? " SHIP" : " SHIPS"));
    }
    if (_battle->lives() != _shownLives || _battle->passed() != _shownPassed)
    {
        _shownLives = _battle->lives();
        _shownPassed = _battle->passed();
        label(_livesKey, std::to_string(_shownLives) + " LIVES / " + std::to_string(_shownPassed) + " PASSED");
    }
    if (_battle->score() != _shownScore)
    {
        _shownScore = _battle->score();
        label(_scoreKey, std::to_string(_shownScore) + " POINTS");
    }
}

void FleetGame::frame(const Input &input, float rawDt)
{
    const float dt = std::min(MAX_DT, rawDt);
    _elapsed += dt;
    _water->setTime(_elapsed);

    if (pushed(input, _dayKey))
    {
        setDay(true);
    }
    if (pushed(input, _nightKey))
    {
        setDay(false);
    }
    if (pushed(input, _fogKey))
    {
        setFog(true);
    }
    if (pushed(input, _clearKey))
    {
        setFog(false);
    }
    if (pushed(input, _speedKey))
    {
        nextSpeed();
    }
    if (pushed(input, _lightsKey))
    {
        nextLightLimit();
    }
    if (pushed(input, _autoFireKey))
    {
        toggleAutoFire();
    }
    if (pushed(input, _waveKey) || pushed(input, _waveKey2))
    {
        _battle->sendWave();
    }

    const float fade = dt / FADE_SECONDS;
    _dayAmount = approach(_dayAmount, _day ? 1.0f : 0.0f, fade);
    _fogAmount = approach(_fogAmount, _fog ? FOG_AMOUNT : 0.0f, fade * FOG_AMOUNT);
    _dayUniform->set(_dayAmount);
    _fogUniform->set(_fogAmount);
    _battle->setWeather(_dayAmount, _fogAmount / FOG_AMOUNT);

    // ALT + turn aims a shot instead of the beam: clockwise sends the marker
    // further out, anticlockwise brings it back, and letting go of ALT fires
    // every lighthouse that was given one. The beam holds still meanwhile:
    // the turn goes into the knob's offset, so letting go of ALT does not
    // snap the beam to where the knob now points.
    const bool alt = input.alt;
    for (int i = 0; i < karl::sdk::ENCODERS; i++)
    {
        // -1 = no reading yet: the beam stays where it was.
        if (input.angle[i] < 0)
        {
            continue;
        }
        const float knob = ANGLE_SIGN * input.angle[i] * TWO_PI / karl::sdk::ANGLE_RESOLUTION;
        KnobState &state = _knobs[i];
        if (!state.read)
        {
            state.read = true;
            state.last = knob;
        }
        const float turned = std::remainder(knob - state.last, TWO_PI);
        state.last = knob;

        // A real turn lights the lighthouse. Summed, so the jitter of a knob
        // nobody touches cancels out, and let fade so it cannot creep up.
        state.activity = state.activity * std::exp(-dt / ACTIVITY_MEMORY) + turned;
        if (std::abs(state.activity) >= START_TURN)
        {
            state.activity = 0;
            _battle->useTower(i);
        }

        if (alt && turned != 0)
        {
            state.offset -= turned;
            // A knob nobody touches still jitters by a tick or two, so a shot
            // is only started by a real turn; jitter cancels out in the sum.
            if (!state.charging)
            {
                state.pending += turned;
                if (std::abs(state.pending) >= START_TURN)
                {
                    state.charging = true;
                    // Clockwise is a falling angle.
                    _battle->charge(i, -state.pending * CHARGE_PER_RADIAN);
                }
            }
            else
            {
                _battle->charge(i, -turned * CHARGE_PER_RADIAN);
            }
        }
        if (!alt)
        {
            state.pending = 0;
            state.charging = false;
        }

        const float radians = knob + state.offset;
        _water->setBeam(i, radians);
        _battle->aimTower(i, osg::Vec2(std::cos(radians), std::sin(radians)));
    }
    if (_wasAlt && !alt)
    {
        _battle->releaseCharges();
    }
    _wasAlt = alt;

    for (int i = 0; i < karl::sdk::ENCODERS; i++)
    {
        // A beam dims when its lighthouse fires and comes back up as it
        // reloads, so the panel shows which ones are ready; a lighthouse
        // that is not lit shows nothing.
        _water->setBeamPower(i, _battle->lightOf(i) * (0.35f + 0.65f * _battle->readiness(i)));
    }

    // Any key but the right-hand column, which is the light and fog's.
    bool anyPushed = false;
    for (int i = 0; i < karl::sdk::KEYS; i++)
    {
        if (pushed(input, i) && !_sideKeys.count(i))
        {
            anyPushed = true;
        }
    }
    _battle->step(dt, anyPushed);
    refreshHud();
}
