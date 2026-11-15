#pragma once

#include "Models.h"
#include "Particles.h"
#include "Tactical.h"

#include <karl/sdk.h>

#include <osg/Group>
#include <osg/MatrixTransform>
#include <osg/Texture2D>
#include <osg/Uniform>

#include <array>
#include <random>
#include <string>
#include <vector>

namespace fleet
{
    class Label;

    /**
     * The tower defence itself. Fleets cross the sea from edge to edge
     * between the islands; each island's lighthouse fires on whatever ship
     * sails into its beam. A ship that gets across costs lives; run out and
     * the game is over until a button is pressed.
     *
     * Everything is in screen pixels, z up from the water.
     */
    class Battle : public osg::Group
    {
    public:
        enum class Mode
        {
            // One canal across the middle of the panel, sailed west to east;
            // the rest of the screen is dark.
            CANAL,
            // Open sea over the whole panel, fleets from every side. The default.
            SEA,
        };

        // Everything is lit by `light`: the beams' texture, see Water.
        Battle(const karl::sdk::Host *host, osg::Texture2D *light, Mode mode);

        // Where lighthouse `index` stands. Its island is built there.
        void placeTower(int index, float x, float y);
        // Where its beam points: a unit vector.
        void aimTower(int index, const osg::Vec2 &direction);

        // Moves lighthouse `index`'s aim marker `delta` pixels out along its
        // beam, starting it if it was not showing. Held between the minimum
        // and how far the shot can go.
        void charge(int index, float delta);
        // Fires every lighthouse with a marker, at its marker, if it is
        // loaded, and clears the markers.
        void releaseCharges();
        // Whether lighthouses fire on their own at ships in their beams.
        void setAutoFire(bool on) { _autoFire = on; }
        // By day nothing is hidden, so every damaged ship shows its bar.
        void setDay(bool day) { _day = day; }
        // Day and fog as they fade, 0..1 each, for the sonar plot.
        void setWeather(float day, float fog) { _tactical->setConditions(day, fog); }

        // Its knob turned: light it, and put out the one used longest ago if
        // that makes too many.
        void useTower(int index);
        // Scales every ship's speed - and the gap between arrivals, so a
        // slower fleet does not bunch up.
        void setSpeed(float speed) { _speed = speed; }
        // How many lighthouses may be lit at once.
        void setLightLimit(int limit);
        // How bright lighthouse `index`'s light is, 0..1, fading on and off.
        float lightOf(int index) const;

        // How ready lighthouse `index` is to fire again: 0 just fired, 1 loaded.
        float readiness(int index) const;

        // For the bottom row of buttons, which shows them.
        int wave() const { return _wave; }
        int score() const { return _score; }
        int lives() const { return _lives; }
        // Ships that got across this game.
        int passed() const { return _passed; }
        // Ships still sailing, the ones still off the edge included.
        int shipsAfloat() const;

        // Sends the next wave now. Again while one is arriving sends another
        // alongside it.
        void sendWave();

        // Advances everything by dt seconds. `pressed` is true on the frame a
        // panel button goes down; it restarts a lost game.
        void step(float dt, bool pressed);

    private:
        struct Ship
        {
            // Unique for the game, for the sonar plot to follow it by.
            int id = 0;
            ShipKind kind;
            osg::Vec2 pos;
            osg::Vec2 goal;
            float heading = 0;
            float speed = 0;
            float turnRate = 0;
            int hp = 1;
            int maxHp = 1;
            float flash = 0;
            float sinking = -1; // seconds since it went down, -1 while afloat
            float wakeTimer = 0;
            float bobPhase = 0;
            osg::ref_ptr<osg::MatrixTransform> node;
            osg::ref_ptr<osg::Uniform> flashUniform;
        };

        struct Tower
        {
            bool placed = false;
            osg::Vec2 pos;
            osg::Vec2 aim{1, 0};
            float cooldown = 0;
            // How far out the hand-aimed shot will land. Negative when no
            // marker is showing.
            float charge = -1;
            // Lit: it shines and it fires. Only so many at a time; turning a
            // knob lights its lighthouse and puts out the one used longest ago.
            bool active = false;
            // 0..1, fading after `active`.
            float light = 0;
            // When its knob last turned, as a running count.
            long lastUsed = 0;
        };

        struct Shot
        {
            osg::Vec2 from;
            osg::Vec2 to;
            float age = 0;
            float duration = 1;
            float arc = 40;
            float trailTimer = 0;
        };

        // Points floating up off a sunk ship.
        struct Popup
        {
            osg::ref_ptr<Label> text;
            osg::Vec2 pos;
            float age = 0;
        };

        enum class Phase
        {
            PLAYING,
            // Out of lives: waiting for a button to start again.
            OVER,
        };

        // One wave sent and still arriving.
        struct Wave
        {
            std::vector<ShipKind> queue;
            float timer = 0;
        };

        void restart();
        void limitLights();
        void spawnShip(ShipKind kind);

        void stepShips(float dt);
        void stepTowers(float dt);
        void stepShots(float dt);
        void stepWaves(float dt);
        void stepPopups(float dt);

        // The nearest ship this lighthouse can see and reach, or null.
        const Ship *targetFor(const Tower &tower) const;
        // How far down its beam a lighthouse can shoot: its range, or short of
        // the first island in the way.
        float reachOf(const Tower &tower) const;
        void fire(Tower &tower, const Ship &target);
        void fireAt(Tower &tower, const osg::Vec2 &at);
        void land(const Shot &shot);
        void sink(Ship &ship);

        void explosion(const osg::Vec2 &at);
        void splash(const osg::Vec2 &at);
        void wake(const Ship &ship);

        // Things redrawn every frame: shadows and health bars.
        void addShadows();
        void addHealthBars();
        void addAimMarkers();

        void setBanner(const std::string &title, const std::string &subtitle);

        // Whether a straight line from a lighthouse to `to` clears every other
        // island.
        bool clearLine(const Tower &from, const osg::Vec2 &to) const;

        // Whether a point is in a beam or by a lighthouse, roughly as the
        // water shader lights it.
        bool isLit(const osg::Vec2 &pos) const;

        float randomFloat(float min, float max);

        std::array<Tower, 24> _towers;
        std::vector<Ship> _ships;
        std::vector<Shot> _shots;
        std::vector<Wave> _waves;
        std::vector<Popup> _popups;

        osg::ref_ptr<osg::Group> _models;
        osg::ref_ptr<osg::Group> _shipNodes;
        osg::ref_ptr<ParticleLayer> _under;
        osg::ref_ptr<ParticleLayer> _fx;
        osg::ref_ptr<ParticleLayer> _glow;
        osg::ref_ptr<ParticleLayer> _bars;
        osg::ref_ptr<Tactical> _tactical;
        std::array<osg::ref_ptr<osg::Geode>, 3> _shipModels;

        osg::ref_ptr<Label> _bannerTitle;
        osg::ref_ptr<Label> _bannerSubtitle;

        const karl::sdk::Host *_host;
        Mode _mode;
        bool _autoFire = false;
        int _lightLimit = 5;
        float _speed = 1.0f;
        long _uses = 0;
        bool _day = false;
        Phase _phase = Phase::PLAYING;
        float _phaseTime = 0;
        // Left until the "Wave n" banner clears.
        float _bannerTime = 0;
        float _time = 0;
        int _wave = 0;
        int _score = 0;
        int _lives = 0;
        int _shipCount = 0;
        int _passed = 0;

        std::mt19937 _random;
    };
}
