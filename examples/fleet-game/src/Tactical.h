#pragma once

#include <karl/sdk.h>

#include <osg/Geometry>
#include <osg/Group>
#include <osg/Uniform>

#include <string>
#include <vector>

namespace fleet
{
    class Label;

    /**
     * A sonar plot laid over the sea. The flex PCB's copper shows through the
     * display as dark lines (karl/traces.h); this reads them as the plot's
     * sector boundaries: small marks where they cross, a coordinate in each
     * cell, and a flash when a ship sails into one. Ships get sonar rings
     * when first detected, when they are dangerous, and in fog.
     *
     * Pure view: Battle reports its ships each frame, nothing here feeds back.
     * Everything is in screen pixels, y up.
     */
    class Tactical : public osg::Group
    {
    public:
        explicit Tactical(const karl::sdk::Host *host);

        // 0 night .. 1 day, 0 clear .. 1 full fog.
        void setConditions(float day, float fog);

        // Ship `id` is at `pos` this frame. Dangerous ships are pinged again
        // every so often. A ship not reported by the next step() is gone.
        void contact(int id, const osg::Vec2 &pos, bool dangerous);
        // A lighthouse has locked on to ship `id`.
        void acquire(int id);

        // Index of the sector containing `pos`, or -1 outside the plot.
        int sectorAt(const osg::Vec2 &pos) const;

        // Animates by dt seconds and rebuilds the moving marks.
        void step(float dt);

    private:
        struct Sector
        {
            float x0, y0, x1, y1;
            std::string name;
            osg::ref_ptr<Label> label;
            // 1 just entered, fading to 0.
            float flash = 0;
            // Last colour written, so an unchanged label costs nothing.
            osg::Vec4 labelColor;
        };

        struct Contact
        {
            int id;
            osg::Vec2 pos;
            int sector = -1;
            bool detected = false;
            bool dangerous = false;
            bool seen = false;
            // Counts down to the next repeat ping.
            float pingTimer = 0;
            // Seconds since detection / lock-on; negative when not showing.
            float pulseAge = -1;
            float acquireAge = -1;
        };

        // A ping: fixed where the ship was when it went out.
        struct Ring
        {
            osg::Vec2 at;
            float age = 0;
            bool danger = false;
        };

        // Sectors and labels from the traces; returns the intersection nodes.
        osg::ref_ptr<osg::Geometry> buildGrid(const karl::sdk::Host *host);
        void detect(Contact &contact);
        void enterSector(int sector, const char *what);
        void ping(const osg::Vec2 &at, bool danger);
        void showMessage(const std::string &text);
        // Sets a text's colour from `alpha` and the conditions, if it changed.
        void setTextAlpha(Label &text, osg::Vec4 &written, float alpha);

        void line(const osg::Vec2 &a, const osg::Vec2 &b, const osg::Vec4 &color);
        void circle(const osg::Vec2 &center, float radius, const osg::Vec4 &color);
        // L-shaped brackets at the four corners of a box.
        void brackets(float x0, float y0, float x1, float y1, float arm, const osg::Vec4 &color);
        void fill(const Sector &sector, const osg::Vec4 &color);

        std::vector<Sector> _sectors;
        std::vector<Contact> _contacts;
        std::vector<Ring> _rings;

        // Moving marks, rebuilt each step into arrays that keep their capacity.
        osg::ref_ptr<osg::Geometry> _lines;
        osg::ref_ptr<osg::Vec3Array> _lineVertices;
        osg::ref_ptr<osg::Vec4Array> _lineColors;
        osg::ref_ptr<osg::DrawArrays> _lineDraw;
        osg::ref_ptr<osg::Geometry> _fills;
        osg::ref_ptr<osg::Vec3Array> _fillVertices;
        osg::ref_ptr<osg::Vec4Array> _fillColors;
        osg::ref_ptr<osg::DrawArrays> _fillDraw;

        osg::ref_ptr<osg::Uniform> _opacityUniform;
        osg::ref_ptr<osg::Uniform> _shadeUniform;
        // Overall alpha scale and colour multiplier from day and fog.
        float _opacity = 1;
        float _shade = 1;
        // Fog thick enough to sweep every contact.
        bool _foggy = false;

        osg::ref_ptr<Label> _message;
        float _messageAge = 1e3f;
        osg::Vec4 _messageColor;
    };
}
