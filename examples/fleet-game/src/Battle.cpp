#include "Battle.h"

#include "View.h"

#include "Label.h"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace fleet;

namespace
{
    constexpr float PI = 3.14159265f;

    constexpr int START_LIVES = 10;
    // How long "Wave n" stays up after a wave is sent.
    constexpr float BANNER_SECONDS = 2.0f;
    // How far off the screen a ship starts, and how far past the far edge it
    // must get to count as through.
    constexpr float OFFSCREEN = 60.0f;

    // Ships steer to keep this far from an island's centre.
    constexpr float ISLAND_CLEARANCE = 85.0f;
    constexpr float SHIP_SPACING = 60.0f;

    // In the canal ships pass close by the lighthouses on its south bank, and
    // keep to the middle of it.
    constexpr float CANAL_ISLAND_CLEARANCE = 62.0f;
    constexpr float CANAL_LANE_Y0 = 380.0f;
    constexpr float CANAL_LANE_Y1 = 420.0f;

    // Lighthouses: how far they reach, how wide a cone they fire into (the
    // cosine of the half-angle), how often, and where the gun sits.
    constexpr float TOWER_RANGE = 300.0f;
    // How close a shot's line may pass to another island's centre before the
    // island is in the way. Water.cpp's beams are shadowed by the same rule.
    constexpr float ISLAND_BLOCK = 40.0f;
    constexpr float TOWER_CONE = 0.94f;
    // Where the drawn beam fades out, as a cosine. The same as the firing
    // cone, so any ship a lighthouse can shoot is one the player can see.
    constexpr float BEAM_EDGE = TOWER_CONE;
    constexpr float TOWER_RELOAD = 1.8f;
    // Shots leave from the rim of the knob over the lighthouse, a little
    // above the water.
    constexpr float MUZZLE_HEIGHT = 12.0f;
    constexpr float KNOB_RADIUS = 36.0f;
    constexpr float SHOT_SPEED = 650.0f;
    constexpr float SINK_SECONDS = 3.5f;
    // How fast a light fades in or out, per second.
    constexpr float LIGHT_FADE = 4.0f;
    // The nearest a hand-aimed shot can land.
    constexpr float MIN_CHARGE = 50.0f;
    constexpr float POPUP_SECONDS = 1.3f;

    struct ShipSpec
    {
        float speed;
        float turnRate;
        int hp;
        // Half the hull's length and width, for hits, shadows and wakes.
        float halfLength;
        float halfWidth;
        float sailHeight;
        int score;
        // Lives lost when it gets across.
        int cost;
    };

    const ShipSpec &spec(ShipKind kind)
    {
        constexpr float S = models::SHIP_SCALE;
        static const ShipSpec GALLEY{62, 1.6f, 2, 22 * S, 7 * S, 30 * S, 10, 1};
        static const ShipSpec CARAVEL{46, 1.1f, 5, 29 * S, 9 * S, 45 * S, 25, 1};
        static const ShipSpec GALLEON{30, 0.7f, 12, 39 * S, 12 * S, 60 * S, 60, 3};
        switch (kind)
        {
        case ShipKind::GALLEY:
            return GALLEY;
        case ShipKind::CARAVEL:
            return CARAVEL;
        default:
            return GALLEON;
        }
    }

    osg::Vec2 direction(float angle)
    {
        return osg::Vec2(std::cos(angle), std::sin(angle));
    }

    // Where the sun throws the shadow of a point at height z.
    osg::Vec2 shadowOffset(float z)
    {
        return osg::Vec2(-SUN_X / SUN_Z * z, -SUN_Y / SUN_Z * z);
    }

    float wrapAngle(float a)
    {
        while (a > PI)
        {
            a -= 2 * PI;
        }
        while (a < -PI)
        {
            a += 2 * PI;
        }
        return a;
    }

    // The middle of the channels between island rows and columns: where a
    // fleet enters.
    const float ROW_LANES[] = {211, 400, 589};
    const float COLUMN_LANES[] = {263, 451, 640, 828, 1016};

    osg::ref_ptr<Label> hudText(const karl::sdk::Host *host, float size, osgText::Text::AlignmentType alignment)
    {
        osg::ref_ptr<Label> text = new Label(host, "");
        text->setCharacterSize(size);
        text->setAlignment(alignment);
        text->setColor(osg::Vec4(1.0f, 0.97f, 0.9f, 1.0f));
        osg::StateSet *state = text->getOrCreateStateSet();
        state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
        state->setRenderBinDetails(BIN_HUD + 1, "RenderBin");
        return text;
    }
}

Battle::Battle(const karl::sdk::Host *host, osg::Texture2D *light, Mode mode)
    : _host(host), _mode(mode), _random(std::random_device{}())
{
    osg::StateSet *state = this->getOrCreateStateSet();
    state->setTextureAttribute(LIGHT_UNIT, light);
    state->addUniform(new osg::Uniform("uLight", LIGHT_UNIT));
    state->addUniform(new osg::Uniform("uScreen", osg::Vec2(DISPLAY_WIDTH, DISPLAY_HEIGHT)));

    _under = new ParticleLayer(ParticleLayer::Blend::ALPHA, BIN_UNDER, true);
    _fx = new ParticleLayer(ParticleLayer::Blend::ALPHA, BIN_FX, true);
    _glow = new ParticleLayer(ParticleLayer::Blend::ADD, BIN_GLOW, false);
    _bars = new ParticleLayer(ParticleLayer::Blend::ALPHA, BIN_HUD, false);

    _models = new osg::Group();
    _models->setStateSet(models::modelState());
    _shipNodes = new osg::Group();
    _models->addChild(_shipNodes);

    _shipModels = {models::ship(ShipKind::GALLEY), models::ship(ShipKind::CARAVEL),
                   models::ship(ShipKind::GALLEON)};

    this->addChild(_under);
    this->addChild(_models);
    this->addChild(_fx);
    this->addChild(_glow);
    this->addChild(_bars);
    _tactical = new Tactical(host);
    this->addChild(_tactical);

    // Small and quiet: a caption over the game, not a poster.
    _bannerTitle = hudText(host, 20, osgText::Text::CENTER_CENTER);
    _bannerTitle->setPos(DISPLAY_WIDTH * 0.5f, DISPLAY_HEIGHT * 0.5f + 9);
    _bannerSubtitle = hudText(host, 12, osgText::Text::CENTER_CENTER);
    _bannerSubtitle->setFont(karl::sdk::Font::REGULAR);
    _bannerSubtitle->setColor(osg::Vec4(0.7f, 0.74f, 0.8f, 1.0f));
    _bannerSubtitle->setPos(DISPLAY_WIDTH * 0.5f, DISPLAY_HEIGHT * 0.5f - 13);
    for (auto &text : {_bannerTitle, _bannerSubtitle})
    {
        this->addChild(text);
    }

    restart();
}

void Battle::placeTower(int index, float x, float y)
{
    Tower &tower = _towers[index];
    tower.placed = true;
    tower.pos = osg::Vec2(x, y);
}

void Battle::aimTower(int index, const osg::Vec2 &direction)
{
    _towers[index].aim = direction;
}

void Battle::useTower(int index)
{
    Tower &tower = _towers[index];
    if (!tower.placed)
    {
        return;
    }
    tower.active = true;
    tower.lastUsed = ++_uses;
    limitLights();
}

void Battle::setLightLimit(int limit)
{
    _lightLimit = limit;
    limitLights();
}

void Battle::limitLights()
{
    // Out goes the light used longest ago, until few enough are left.
    for (;;)
    {
        Tower *oldest = nullptr;
        int lit = 0;
        for (Tower &tower : _towers)
        {
            if (!tower.active)
            {
                continue;
            }
            lit++;
            if (!oldest || tower.lastUsed < oldest->lastUsed)
            {
                oldest = &tower;
            }
        }
        if (lit <= _lightLimit || !oldest)
        {
            return;
        }
        oldest->active = false;
        oldest->charge = -1;
    }
}

float Battle::lightOf(int index) const
{
    return _towers[index].light;
}

float Battle::readiness(int index) const
{
    return 1.0f - _towers[index].cooldown / TOWER_RELOAD;
}

void Battle::restart()
{
    for (Ship &ship : _ships)
    {
        _shipNodes->removeChild(ship.node);
    }
    _ships.clear();
    _shots.clear();
    _waves.clear();
    for (Popup &popup : _popups)
    {
        this->removeChild(popup.text);
    }
    _popups.clear();
    _fx->clear();
    _glow->clear();
    for (Tower &tower : _towers)
    {
        tower.cooldown = 0;
    }
    _wave = 0;
    _score = 0;
    _lives = START_LIVES;
    _passed = 0;
    _phase = Phase::PLAYING;
    _phaseTime = 0;
    _bannerTime = 0;
    setBanner("Press WAVE to send a fleet", "Turn a knob to light its lighthouse");
}

void Battle::sendWave()
{
    if (_phase == Phase::OVER)
    {
        return;
    }
    _wave++;

    // A single galley first, then slowly more: caravels from wave 3,
    // galleons from wave 4. Shuffled so the big ones are not always last.
    Wave wave;
    wave.queue.insert(wave.queue.end(), 1 + _wave / 2, ShipKind::GALLEY);
    wave.queue.insert(wave.queue.end(), _wave / 3, ShipKind::CARAVEL);
    wave.queue.insert(wave.queue.end(), _wave >= 4 ? (_wave - 2) / 2 : 0, ShipKind::GALLEON);
    std::shuffle(wave.queue.begin(), wave.queue.end(), _random);
    _waves.push_back(wave);

    setBanner("Wave " + std::to_string(_wave), "");
    _bannerTime = BANNER_SECONDS;
}

void Battle::spawnShip(ShipKind kind)
{
    const ShipSpec &s = spec(kind);

    // The first waves all come from the west; later ones from any side. In
    // the canal, always west to east, on the lane the islands leave open.
    const int side = _mode == Mode::CANAL || _wave <= 2 ? 0 : static_cast<int>(randomFloat(0, 4));
    const float jitter = randomFloat(-25, 25);
    Ship ship;
    ship.id = ++_shipCount;
    ship.kind = kind;
    if (_mode == Mode::CANAL)
    {
        const float y = randomFloat(CANAL_LANE_Y0, CANAL_LANE_Y1);
        ship.pos = osg::Vec2(CANAL_X0 - OFFSCREEN, y);
        ship.goal = osg::Vec2(CANAL_X1 + OFFSCREEN * 2, y);
    }
    else if (side == 0 || side == 1)
    {
        const float y = ROW_LANES[static_cast<int>(randomFloat(0, 3)) % 3] + jitter;
        const float west = -OFFSCREEN, east = DISPLAY_WIDTH + OFFSCREEN;
        ship.pos = osg::Vec2(side == 0 ? west : east, y);
        ship.goal = osg::Vec2(side == 0 ? east + OFFSCREEN : west - OFFSCREEN, y);
    }
    else
    {
        const float x = COLUMN_LANES[static_cast<int>(randomFloat(0, 5)) % 5] + jitter;
        const float south = -OFFSCREEN, north = DISPLAY_HEIGHT + OFFSCREEN;
        ship.pos = osg::Vec2(x, side == 2 ? south : north);
        ship.goal = osg::Vec2(x, side == 2 ? north + OFFSCREEN : south - OFFSCREEN);
    }
    const osg::Vec2 toGoal = ship.goal - ship.pos;
    ship.heading = std::atan2(toGoal.y(), toGoal.x());
    ship.speed = s.speed * randomFloat(0.9f, 1.1f);
    ship.turnRate = s.turnRate;
    ship.hp = ship.maxHp = s.hp;
    ship.bobPhase = randomFloat(0, 2 * PI);

    ship.node = new osg::MatrixTransform();
    ship.node->addChild(_shipModels[static_cast<int>(kind)]);
    ship.flashUniform = new osg::Uniform("uFlash", 0.0f);
    ship.node->getOrCreateStateSet()->addUniform(ship.flashUniform);
    _shipNodes->addChild(ship.node);

    _ships.push_back(ship);
}

void Battle::step(float dt, bool pressed)
{
    _time += dt;
    _phaseTime += dt;

    if (_phase == Phase::OVER && pressed && _phaseTime > 1.0f)
    {
        restart();
    }

    _under->step(dt);
    _fx->step(dt);
    _glow->step(dt);
    _bars->step(dt);

    stepWaves(dt);
    stepShips(dt);
    if (_phase != Phase::OVER)
    {
        stepTowers(dt);
    }
    stepShots(dt);
    stepPopups(dt);

    addShadows();
    addHealthBars();
    addAimMarkers();
    for (const Ship &ship : _ships)
    {
        if (ship.sinking < 0)
        {
            _tactical->contact(ship.id, ship.pos, ship.kind == ShipKind::GALLEON);
        }
    }
    _tactical->step(dt);

    _under->draw();
    _fx->draw();
    _glow->draw();
    _bars->draw();
}

void Battle::stepWaves(float dt)
{
    if (_bannerTime > 0)
    {
        _bannerTime -= dt;
        if (_bannerTime <= 0 && _phase != Phase::OVER)
        {
            setBanner("", "");
        }
    }
    if (_phase == Phase::OVER)
    {
        return;
    }

    // Every wave sent spawns on its own clock, so several sent at once
    // arrive side by side.
    for (Wave &wave : _waves)
    {
        wave.timer -= dt;
        if (wave.timer > 0 || wave.queue.empty())
        {
            continue;
        }
        spawnShip(wave.queue.back());
        wave.queue.pop_back();
        wave.timer = std::max(0.7f, 2.2f - _wave * 0.12f) * randomFloat(0.7f, 1.3f) / _speed;
    }
    _waves.erase(std::remove_if(_waves.begin(), _waves.end(), [](const Wave &wave)
                                { return wave.queue.empty(); }),
                 _waves.end());
}

void Battle::stepShips(float dt)
{
    for (Ship &ship : _ships)
    {
        ship.flash = std::max(0.0f, ship.flash - dt * 6);
        ship.flashUniform->set(ship.flash);

        if (ship.sinking >= 0)
        {
            ship.sinking += dt;
            ship.wakeTimer -= dt;
            if (ship.wakeTimer <= 0 && ship.sinking < SINK_SECONDS * 0.7f)
            {
                ship.wakeTimer = 0.12f;
                Particle smoke;
                smoke.pos = osg::Vec3(ship.pos.x() + randomFloat(-8, 8), ship.pos.y() + randomFloat(-5, 5), 10);
                smoke.vel = osg::Vec3(randomFloat(-6, 6), randomFloat(-4, 4), 28);
                smoke.size = 5;
                smoke.grow = 9;
                smoke.life = 2.2f;
                const float grey = randomFloat(0.15f, 0.3f);
                smoke.color = osg::Vec4(grey, grey, grey, 0.55f);
                _fx->add(smoke);

                Particle bubble;
                bubble.pos = osg::Vec3(ship.pos.x() + randomFloat(-14, 14), ship.pos.y() + randomFloat(-8, 8), 0);
                bubble.size = 3;
                bubble.grow = 10;
                bubble.life = 1.2f;
                bubble.color = osg::Vec4(0.95f, 0.98f, 1.0f, 0.5f);
                _under->add(bubble);
            }
            const float t = ship.sinking / SINK_SECONDS;
            const float roll = 0.55f * std::min(1.0f, t * 2.0f);
            const float pitch = 0.25f * t;
            const float depth = -45.0f * t * t;
            ship.node->setMatrix(osg::Matrix::rotate(roll, osg::Vec3(1, 0, 0)) *
                                 osg::Matrix::rotate(pitch, osg::Vec3(0, 1, 0)) *
                                 osg::Matrix::rotate(ship.heading, osg::Vec3(0, 0, 1)) *
                                 osg::Matrix::translate(ship.pos.x(), ship.pos.y(), depth));
            continue;
        }

        // Steer for the goal, away from islands and from each other.
        osg::Vec2 desired = ship.goal - ship.pos;
        desired.normalize();
        for (const Tower &tower : _towers)
        {
            if (!tower.placed)
            {
                continue;
            }
            osg::Vec2 away = ship.pos - tower.pos;
            const float d = away.length();
            const float clearance = _mode == Mode::CANAL ? CANAL_ISLAND_CLEARANCE : ISLAND_CLEARANCE;
            if (d < clearance && d > 0.01f)
            {
                away /= d;
                desired += away * (2.5f * (1.0f - d / clearance));
            }
        }
        // The canal's banks push back.
        if (_mode == Mode::CANAL)
        {
            const float margin = spec(ship.kind).halfWidth + 8;
            if (ship.pos.y() < CANAL_Y0 + margin)
            {
                desired += osg::Vec2(0, 1.5f);
            }
            if (ship.pos.y() > CANAL_Y1 - margin)
            {
                desired += osg::Vec2(0, -1.5f);
            }
        }
        for (const Ship &other : _ships)
        {
            if (&other == &ship || other.sinking >= 0)
            {
                continue;
            }
            osg::Vec2 away = ship.pos - other.pos;
            const float d = away.length();
            if (d < SHIP_SPACING && d > 0.01f)
            {
                away /= d;
                desired += away * (1.2f * (1.0f - d / SHIP_SPACING));
            }
        }
        const float want = std::atan2(desired.y(), desired.x());
        const float turn = std::clamp(wrapAngle(want - ship.heading), -ship.turnRate * dt, ship.turnRate * dt);
        ship.heading = wrapAngle(ship.heading + turn);
        ship.pos += direction(ship.heading) * (ship.speed * _speed * dt);

        const float bob = std::sin(_time * 2.2f + ship.bobPhase);
        ship.node->setMatrix(osg::Matrix::rotate(bob * 0.06f - turn / std::max(dt, 1e-3f) * 0.08f, osg::Vec3(1, 0, 0)) *
                             osg::Matrix::rotate(ship.heading, osg::Vec3(0, 0, 1)) *
                             osg::Matrix::translate(ship.pos.x(), ship.pos.y(), bob * 0.8f));

        ship.wakeTimer -= dt;
        if (ship.wakeTimer <= 0)
        {
            ship.wakeTimer = 0.09f;
            wake(ship);
        }
    }

    // Through: past the goal's edge. Sunk: finished going under.
    for (auto it = _ships.begin(); it != _ships.end();)
    {
        const bool through = it->sinking < 0 &&
                             (it->pos - it->goal).length() < OFFSCREEN * 1.5f;
        const bool sunk = it->sinking >= SINK_SECONDS;
        if (!through && !sunk)
        {
            ++it;
            continue;
        }
        if (through && _phase != Phase::OVER)
        {
            _lives = std::max(0, _lives - spec(it->kind).cost);
            _passed++;
            if (_lives == 0)
            {
                _phase = Phase::OVER;
                _phaseTime = 0;
                _waves.clear();
                setBanner("The fleet broke through",
                          "Wave " + std::to_string(_wave) + "  -  " + std::to_string(_score) +
                              " points  -  press a button to play again");
            }
        }
        _shipNodes->removeChild(it->node);
        it = _ships.erase(it);
    }
}

void Battle::stepTowers(float dt)
{
    for (Tower &tower : _towers)
    {
        if (!tower.placed)
        {
            continue;
        }
        tower.light = tower.active ? std::min(1.0f, tower.light + dt * LIGHT_FADE)
                                   : std::max(0.0f, tower.light - dt * LIGHT_FADE);
        tower.cooldown = std::max(0.0f, tower.cooldown - dt);
        if (tower.cooldown > 0 || !tower.active)
        {
            continue;
        }

        if (!_autoFire)
        {
            continue;
        }
        const Ship *target = targetFor(tower);
        if (target)
        {
            fire(tower, *target);
            tower.cooldown = TOWER_RELOAD;
        }
    }
}

const Battle::Ship *Battle::targetFor(const Tower &tower) const
{
    const Ship *target = nullptr;
    float nearest = TOWER_RANGE;
    for (const Ship &ship : _ships)
    {
        if (ship.sinking >= 0)
        {
            continue;
        }
        const osg::Vec2 to = ship.pos - tower.pos;
        const float d = to.length();
        if (d < 1.0f || d >= nearest || (to * tower.aim) / d < TOWER_CONE ||
            !clearLine(tower, ship.pos))
        {
            continue;
        }
        nearest = d;
        target = &ship;
    }
    return target;
}

float Battle::reachOf(const Tower &tower) const
{
    float reach = TOWER_RANGE;
    for (const Tower &other : _towers)
    {
        if (!other.placed || &other == &tower)
        {
            continue;
        }
        const osg::Vec2 toIsland = other.pos - tower.pos;
        const float along = toIsland * tower.aim;
        const float miss = (toIsland - tower.aim * along).length();
        if (along > 0 && miss < ISLAND_BLOCK)
        {
            reach = std::min(reach, along - ISLAND_BLOCK);
        }
    }
    return std::max(reach, MIN_CHARGE);
}

void Battle::charge(int index, float delta)
{
    Tower &tower = _towers[index];
    if (!tower.placed || !tower.active)
    {
        return;
    }
    const float from = tower.charge < 0 ? MIN_CHARGE : tower.charge;
    tower.charge = std::clamp(from + delta, MIN_CHARGE, reachOf(tower));
}

void Battle::releaseCharges()
{
    for (Tower &tower : _towers)
    {
        if (tower.charge < 0)
        {
            continue;
        }
        if (tower.cooldown <= 0 && tower.active && _phase != Phase::OVER)
        {
            fireAt(tower, tower.pos + tower.aim * tower.charge);
            tower.cooldown = TOWER_RELOAD;
        }
        tower.charge = -1;
    }
}

void Battle::fire(Tower &tower, const Ship &target)
{
    // Lead the target by the flight time, so a moving ship can be hit.
    _tactical->acquire(target.id);
    const float duration = 0.35f + (target.pos - tower.pos).length() / SHOT_SPEED;
    fireAt(tower, target.pos + direction(target.heading) * (target.speed * _speed * duration) +
                      osg::Vec2(randomFloat(-6, 6), randomFloat(-6, 6)));
}

void Battle::fireAt(Tower &tower, const osg::Vec2 &at)
{
    osg::Vec2 heading = at - tower.pos;
    heading.normalize();
    const osg::Vec2 muzzle = tower.pos + heading * KNOB_RADIUS;
    const float distance = (at - muzzle).length();
    Shot shot;
    shot.duration = 0.35f + distance / SHOT_SPEED;
    shot.from = muzzle;
    shot.to = at;
    shot.arc = 30 + distance * 0.18f;
    _shots.push_back(shot);

    Particle flash;
    flash.pos = osg::Vec3(muzzle.x(), muzzle.y(), MUZZLE_HEIGHT);
    flash.size = 10;
    flash.grow = 120;
    flash.life = 0.2f;
    flash.color = osg::Vec4(1.0f, 0.8f, 0.45f, 1.0f);
    _glow->add(flash);

    for (int i = 0; i < 4; i++)
    {
        Particle puff;
        puff.pos = flash.pos;
        puff.vel = osg::Vec3(tower.aim.x() * 30 + randomFloat(-10, 10), tower.aim.y() * 30 + randomFloat(-10, 10), 15);
        puff.drag = 0.4f;
        puff.size = 4;
        puff.grow = 10;
        puff.life = 1.1f;
        puff.color = osg::Vec4(0.85f, 0.85f, 0.82f, 0.5f);
        _fx->add(puff);
    }
}

void Battle::stepShots(float dt)
{
    for (auto it = _shots.begin(); it != _shots.end();)
    {
        Shot &shot = *it;
        shot.age += dt;
        if (shot.age >= shot.duration)
        {
            land(shot);
            it = _shots.erase(it);
            continue;
        }

        const float t = shot.age / shot.duration;
        const osg::Vec2 ground = shot.from + (shot.to - shot.from) * t;
        const float z = MUZZLE_HEIGHT * (1 - t) + shot.arc * 4 * t * (1 - t);

        // A ball of fire: a white-hot core in an orange glow.
        Particle core;
        core.pos = osg::Vec3(ground.x(), ground.y(), z);
        core.size = 3.4f;
        core.life = 1e-4f;
        core.shape = ParticleShape::HARD;
        core.color = osg::Vec4(1.0f, 0.97f, 0.85f, 1.0f);
        core.endAlpha = 1.0f;
        _glow->add(core);

        Particle glow = core;
        glow.size = 14.0f;
        glow.shape = ParticleShape::SOFT;
        glow.color = osg::Vec4(1.0f, 0.62f, 0.25f, 0.9f);
        glow.endAlpha = glow.color.a();
        _glow->add(glow);

        const osg::Vec2 offset = shadowOffset(z);
        Particle shadow;
        shadow.pos = osg::Vec3(ground.x() + offset.x(), ground.y() + offset.y(), 0);
        shadow.size = 3.0f;
        shadow.life = 1e-4f;
        shadow.color = osg::Vec4(0.0f, 0.06f, 0.1f, 0.35f);
        shadow.endAlpha = shadow.color.a();
        _under->add(shadow);

        shot.trailTimer -= dt;
        if (shot.trailTimer <= 0)
        {
            shot.trailTimer = 0.025f;
            Particle ember;
            ember.pos = core.pos;
            ember.size = 6.5f;
            ember.grow = -6;
            ember.life = 0.35f;
            ember.color = osg::Vec4(1.0f, 0.5f, 0.15f, 0.7f);
            _glow->add(ember);

            Particle trail;
            trail.pos = core.pos + osg::Vec3(0, 0, 2);
            trail.vel = osg::Vec3(0, 0, 8);
            trail.size = 2.5f;
            trail.grow = 7;
            trail.life = 0.7f;
            trail.color = osg::Vec4(0.55f, 0.52f, 0.5f, 0.3f);
            _fx->add(trail);
        }
        ++it;
    }
}

void Battle::stepPopups(float dt)
{
    for (auto it = _popups.begin(); it != _popups.end();)
    {
        it->age += dt;
        if (it->age >= POPUP_SECONDS)
        {
            this->removeChild(it->text);
            it = _popups.erase(it);
            continue;
        }
        const float t = it->age / POPUP_SECONDS;
        it->text->setPos(it->pos + osg::Vec2(0, 34 * t));
        it->text->setColor(osg::Vec4(1.0f, 0.86f, 0.35f, 1.0f - t * t));
        ++it;
    }
}

void Battle::land(const Shot &shot)
{
    for (Ship &ship : _ships)
    {
        if (ship.sinking >= 0)
        {
            continue;
        }
        // Hit test against the hull as an ellipse along the heading.
        const ShipSpec &s = spec(ship.kind);
        const osg::Vec2 d = shot.to - ship.pos;
        const osg::Vec2 along = direction(ship.heading);
        const float u = (d * along) / (s.halfLength + 5);
        const float v = (d * osg::Vec2(-along.y(), along.x())) / (s.halfWidth + 6);
        if (u * u + v * v > 1.0f)
        {
            continue;
        }
        ship.hp--;
        ship.flash = 1.0f;
        explosion(shot.to);
        if (ship.hp <= 0)
        {
            sink(ship);
        }
        return;
    }
    splash(shot.to);
}

void Battle::sink(Ship &ship)
{
    ship.sinking = 0;
    ship.wakeTimer = 0;
    const int points = spec(ship.kind).score;
    _score += points;

    Popup popup;
    popup.pos = ship.pos + osg::Vec2(0, spec(ship.kind).sailHeight * OBLIQUE);
    popup.text = hudText(_host, 22, osgText::Text::CENTER_CENTER);
    popup.text->setText("+" + std::to_string(points));
    popup.text->setPos(popup.pos);
    this->addChild(popup.text);
    _popups.push_back(popup);

    Particle blast;
    blast.pos = osg::Vec3(ship.pos.x(), ship.pos.y(), 10);
    blast.size = 10;
    blast.grow = 90;
    blast.life = 0.4f;
    blast.color = osg::Vec4(1.0f, 0.65f, 0.3f, 0.9f);
    _glow->add(blast);
    for (int i = 0; i < 3; i++)
    {
        explosion(ship.pos + osg::Vec2(randomFloat(-15, 15), randomFloat(-8, 8)));
    }
}

void Battle::explosion(const osg::Vec2 &at)
{
    for (int i = 0; i < 6; i++)
    {
        Particle fire;
        fire.pos = osg::Vec3(at.x() + randomFloat(-4, 4), at.y() + randomFloat(-4, 4), randomFloat(4, 12));
        fire.vel = osg::Vec3(randomFloat(-20, 20), randomFloat(-20, 20), randomFloat(10, 40));
        fire.drag = 0.2f;
        fire.size = randomFloat(5, 9);
        fire.grow = 45;
        fire.life = randomFloat(0.25f, 0.45f);
        fire.color = osg::Vec4(1.0f, randomFloat(0.45f, 0.75f), 0.2f, 0.9f);
        _glow->add(fire);
    }
    for (int i = 0; i < 7; i++)
    {
        Particle smoke;
        smoke.pos = osg::Vec3(at.x() + randomFloat(-5, 5), at.y() + randomFloat(-5, 5), 8);
        smoke.vel = osg::Vec3(randomFloat(-18, 18), randomFloat(-18, 18), randomFloat(20, 45));
        smoke.drag = 0.35f;
        smoke.size = 6;
        smoke.grow = 18;
        smoke.life = randomFloat(1.2f, 2.0f);
        const float grey = randomFloat(0.18f, 0.35f);
        smoke.color = osg::Vec4(grey, grey * 0.95f, grey * 0.9f, 0.6f);
        _fx->add(smoke);
    }
    for (int i = 0; i < 9; i++)
    {
        const float a = randomFloat(0, 2 * PI);
        const float speed = randomFloat(30, 80);
        Particle chip;
        chip.pos = osg::Vec3(at.x(), at.y(), 8);
        chip.vel = osg::Vec3(std::cos(a) * speed, std::sin(a) * speed, randomFloat(60, 130));
        chip.gravity = 320;
        chip.size = randomFloat(1.2f, 2.2f);
        chip.life = 2.0f;
        chip.endAlpha = 1.0f;
        chip.shape = ParticleShape::HARD;
        chip.color = osg::Vec4(0.38f, 0.24f, 0.12f, 1.0f);
        _fx->add(chip);
    }
}

void Battle::splash(const osg::Vec2 &at)
{
    for (int i = 0; i < 14; i++)
    {
        const float a = randomFloat(0, 2 * PI);
        const float speed = randomFloat(10, 45);
        Particle drop;
        drop.pos = osg::Vec3(at.x(), at.y(), 1);
        drop.vel = osg::Vec3(std::cos(a) * speed, std::sin(a) * speed, randomFloat(70, 150));
        drop.gravity = 380;
        drop.size = randomFloat(1.2f, 2.0f);
        drop.life = 2.0f;
        drop.endAlpha = 0.9f;
        drop.shape = ParticleShape::HARD;
        drop.color = osg::Vec4(0.92f, 0.97f, 1.0f, 0.9f);
        _fx->add(drop);
    }
    for (int i = 0; i < 12; i++)
    {
        const float a = 2 * PI * i / 12;
        Particle foam;
        foam.pos = osg::Vec3(at.x(), at.y(), 0);
        foam.vel = osg::Vec3(std::cos(a) * 32, std::sin(a) * 32, 0);
        foam.drag = 0.15f;
        foam.size = 3;
        foam.grow = 6;
        foam.life = 1.0f;
        foam.color = osg::Vec4(0.95f, 0.98f, 1.0f, 0.6f);
        _under->add(foam);
    }
    Particle column;
    column.pos = osg::Vec3(at.x(), at.y(), 6);
    column.vel = osg::Vec3(0, 0, 30);
    column.size = 5;
    column.grow = 14;
    column.life = 0.45f;
    column.color = osg::Vec4(0.95f, 0.98f, 1.0f, 0.7f);
    _fx->add(column);
}

void Battle::wake(const Ship &ship)
{
    const ShipSpec &s = spec(ship.kind);
    const osg::Vec2 along = direction(ship.heading);
    const osg::Vec2 side(-along.y(), along.x());
    const osg::Vec2 stern = ship.pos - along * (s.halfLength * 0.9f);
    const osg::Vec2 bow = ship.pos + along * (s.halfLength * 0.85f);
    for (int sign : {-1, 1})
    {
        Particle foam;
        const osg::Vec2 at = stern + side * (sign * s.halfWidth * 0.5f);
        foam.pos = osg::Vec3(at.x(), at.y(), 0);
        const osg::Vec2 drift = side * (sign * 9.0f) - along * 6.0f;
        foam.vel = osg::Vec3(drift.x(), drift.y(), 0);
        foam.size = s.halfWidth * 0.35f;
        foam.grow = 8;
        foam.life = 1.3f;
        foam.color = osg::Vec4(0.95f, 0.98f, 1.0f, 0.42f);
        _under->add(foam);

        Particle spray;
        const osg::Vec2 bowAt = bow + side * (sign * s.halfWidth * 0.3f);
        spray.pos = osg::Vec3(bowAt.x(), bowAt.y(), 0);
        const osg::Vec2 out = side * (sign * 16.0f);
        spray.vel = osg::Vec3(out.x(), out.y(), 0);
        spray.drag = 0.2f;
        spray.size = 2;
        spray.grow = 8;
        spray.life = 0.6f;
        spray.color = osg::Vec4(0.95f, 0.98f, 1.0f, 0.5f);
        _under->add(spray);
    }
}

void Battle::addShadows()
{
    for (const Ship &ship : _ships)
    {
        const ShipSpec &s = spec(ship.kind);
        const float fade = ship.sinking < 0 ? 1.0f : std::max(0.0f, 1.0f - ship.sinking / SINK_SECONDS);

        // The hull, and higher and fainter, the sails.
        for (int layer = 0; layer < 2; layer++)
        {
            const float height = layer == 0 ? 4.0f : s.sailHeight * 0.6f;
            const osg::Vec2 at = ship.pos + shadowOffset(height);
            Particle shadow;
            shadow.pos = osg::Vec3(at.x(), at.y(), 0);
            shadow.size = layer == 0 ? s.halfWidth + 3 : s.halfWidth * 1.2f;
            shadow.stretch = layer == 0 ? s.halfLength / s.halfWidth : 1.3f;
            shadow.angle = layer == 0 ? ship.heading : ship.heading + PI * 0.5f;
            shadow.life = 1e-4f;
            shadow.color = osg::Vec4(0.0f, 0.06f, 0.1f, (layer == 0 ? 0.38f : 0.2f) * fade);
            shadow.endAlpha = shadow.color.a();
            _under->add(shadow);
        }
    }
}

void Battle::addHealthBars()
{
    auto rect = [&](const osg::Vec3 &center, float halfWidth, float halfHeight, const osg::Vec4 &color)
    {
        Particle bar;
        bar.pos = center;
        bar.size = halfHeight;
        bar.stretch = halfWidth / halfHeight;
        bar.life = 1e-4f;
        bar.shape = ParticleShape::RECT;
        bar.color = color;
        bar.endAlpha = color.a();
        _bars->add(bar);
    };

    for (const Ship &ship : _ships)
    {
        // Not in the dark: a bar would give away a ship the night hides.
        if (ship.sinking >= 0 || ship.hp == ship.maxHp || (!_day && !isLit(ship.pos)))
        {
            continue;
        }
        const ShipSpec &s = spec(ship.kind);
        const float halfWidth = 4.0f + s.halfLength * 0.45f;
        const osg::Vec3 center(ship.pos.x(), ship.pos.y() + 14, s.sailHeight + 8);
        const float left = static_cast<float>(ship.hp) / ship.maxHp;
        const osg::Vec4 color = left > 0.5f ? osg::Vec4(0.35f, 0.9f, 0.3f, 1) : left > 0.25f ? osg::Vec4(0.95f, 0.8f, 0.2f, 1) : osg::Vec4(0.95f, 0.3f, 0.2f, 1);
        rect(center, halfWidth + 1.2f, 3.0f, osg::Vec4(0.05f, 0.05f, 0.05f, 0.8f));
        rect(center - osg::Vec3(halfWidth * (1 - left), 0, 0), halfWidth * left, 1.8f, color);
    }

    // Behind the banner, so it reads over bright water.
    if (!_bannerTitle->getText().empty())
    {
        // Two blobs, a wide soft one and a tighter one, so the shade is dark
        // under the text and still has no visible edge.
        for (float scale : {1.0f, 0.6f})
        {
            Particle shade;
            shade.pos = osg::Vec3(DISPLAY_WIDTH * 0.5f, DISPLAY_HEIGHT * 0.5f, 0);
            shade.size = 70 * scale;
            shade.stretch = 3.4f;
            shade.life = 1e-4f;
            shade.color = osg::Vec4(0.0f, 0.04f, 0.08f, 0.45f);
            shade.endAlpha = shade.color.a();
            _bars->add(shade);
        }
    }
}

void Battle::addAimMarkers()
{
    auto dot = [&](const osg::Vec2 &at, float size, const osg::Vec4 &color)
    {
        Particle p;
        p.pos = osg::Vec3(at.x(), at.y(), 0);
        p.size = size;
        p.life = 1e-4f;
        p.shape = ParticleShape::HARD;
        p.color = color;
        p.endAlpha = color.a();
        _bars->add(p);
    };

    for (const Tower &tower : _towers)
    {
        if (tower.charge < 0)
        {
            continue;
        }
        // Red when it will fire on release, grey while it is still loading.
        const bool loaded = tower.cooldown <= 0;
        const osg::Vec4 line = loaded ? osg::Vec4(1.0f, 0.78f, 0.4f, 0.75f) : osg::Vec4(0.6f, 0.6f, 0.6f, 0.4f);
        const osg::Vec4 mark = loaded ? osg::Vec4(1.0f, 0.32f, 0.18f, 0.95f) : osg::Vec4(0.6f, 0.6f, 0.6f, 0.6f);

        // Dotted down the middle of the beam, out to the marker: how much.
        for (float d = 26; d < tower.charge - 14; d += 14)
        {
            dot(tower.pos + tower.aim * d, 1.6f, line);
        }
        // A ring round where it will land: where.
        const osg::Vec2 at = tower.pos + tower.aim * tower.charge;
        constexpr int RING = 12;
        for (int i = 0; i < RING; i++)
        {
            const float a = 2 * PI * i / RING + _time * 1.5f;
            dot(at + direction(a) * 13.0f, 1.7f, mark);
        }
        dot(at, 2.4f, mark);
    }
}

int Battle::shipsAfloat() const
{
    return static_cast<int>(std::count_if(_ships.begin(), _ships.end(), [](const Ship &ship)
                                          { return ship.sinking < 0; }));
}

void Battle::setBanner(const std::string &title, const std::string &subtitle)
{
    std::string caps = title;
    std::transform(caps.begin(), caps.end(), caps.begin(), [](unsigned char c)
                   { return static_cast<char>(std::toupper(c)); });
    _bannerTitle->setText(caps);
    _bannerSubtitle->setText(subtitle);
}

bool Battle::isLit(const osg::Vec2 &pos) const
{
    for (const Tower &tower : _towers)
    {
        if (!tower.placed || !tower.active)
        {
            continue;
        }
        const osg::Vec2 to = pos - tower.pos;
        const float d = to.length();
        if (d < ISLAND_CLEARANCE)
        {
            return true;
        }
        if (d < TOWER_RANGE && (to * tower.aim) / d > BEAM_EDGE && clearLine(tower, pos))
        {
            return true;
        }
    }
    return false;
}

bool Battle::clearLine(const Tower &from, const osg::Vec2 &to) const
{
    const osg::Vec2 line = to - from.pos;
    const float length2 = std::max(line.length2(), 1.0f);
    for (const Tower &other : _towers)
    {
        if (!other.placed || &other == &from)
        {
            continue;
        }
        const osg::Vec2 toIsland = other.pos - from.pos;
        const float t = std::clamp((toIsland * line) / length2, 0.0f, 1.0f);
        if ((toIsland - line * t).length() < ISLAND_BLOCK)
        {
            return false;
        }
    }
    return true;
}

float Battle::randomFloat(float min, float max)
{
    return std::uniform_real_distribution<float>(min, max)(_random);
}
