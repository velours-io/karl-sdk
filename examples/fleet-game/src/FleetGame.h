#pragma once

#include <karl/sdk.h>

#include <osg/Group>
#include <osg/Uniform>

#include <array>
#include <set>
#include <string>

namespace fleet
{
    class Battle;
    class Water;

    /**
     * A naval tower defence on the panel. Each encoder is a lighthouse on an
     * island: turning it swings the beam, and the lighthouse fires on any ship
     * that sails into its light. By hand: hold ALT and turn knobs to set how
     * far each shot goes, let go to fire them all; auto fire can be switched
     * off.
     */
    class FleetGame
    {
    public:
        FleetGame(const karl::sdk::Host *host, osg::Group *root, const std::string &mode);
        ~FleetGame();

        void frame(const karl::sdk::Input &input, float dt);

    private:
        // The right-hand column: DAY / NIGHT, FOG / CLEAR, SPEED, LIGHTS, auto fire. Each
        // lights the active button of its pair.
        void setDay(bool day);
        void setFog(bool fog);
        void toggleAutoFire();
        void nextLightLimit();
        void nextSpeed();
        void refreshSideButtons();
        // The bottom row's WAVE n / score / LIVES, when they change.
        void refreshHud();
        // Claims a key and returns its index.
        int claim(karl::sdk::Edge edge, int slot, int span, const char *label);
        void light(int key, bool active);
        void label(int key, const std::string &text);
        static bool pushed(const karl::sdk::Input &input, int key);

        // What a knob was doing last frame, for ALT + turn to aim.
        struct KnobState
        {
            bool read = false;
            float last = 0;
            // Added to the knob's angle to give the beam's: what ALT turns
            // have taken out, so the beam stayed put through them.
            float offset = 0;
            // Turned with ALT held, before it counts as starting a shot.
            float pending = 0;
            bool charging = false;
            // Recent turning, for lighting the lighthouse.
            float activity = 0;
        };

        const karl::sdk::Host *_host;
        osg::ref_ptr<osg::Group> _scene;
        osg::ref_ptr<Water> _water;
        osg::ref_ptr<Battle> _battle;
        float _elapsed = 0;

        // What the buttons asked for, and where the fade towards it is.
        bool _day = false;
        bool _fog = true;
        float _dayAmount = 0.0f;
        float _fogAmount = 0.0f;
        osg::ref_ptr<osg::Uniform> _dayUniform;
        osg::ref_ptr<osg::Uniform> _fogUniform;

        std::array<KnobState, karl::sdk::ENCODERS> _knobs;
        // Off by default: the fleet is sized for aiming by hand.
        bool _autoFire = false;
        // Index into LIGHT_LIMITS.
        int _lightLimit;
        // Index into SPEEDS.
        int _speed;
        bool _wasAlt = false;

        // Key indices. The right-hand column is the settings', the bottom
        // row SEND WAVE and the game's numbers.
        int _dayKey, _nightKey, _fogKey, _clearKey, _speedKey, _lightsKey, _autoFireKey;
        int _waveKey, _waveKey2, _waveInfoKey, _shipsKey, _livesKey, _scoreKey;
        std::set<int> _sideKeys;
        int _shownWave = -1;
        int _shownShips = -1;
        int _shownLives = -1;
        int _shownPassed = -1;
        int _shownScore = -1;
    };
}
