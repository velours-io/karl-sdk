#pragma once

#include <osg/Group>

#include <osg/Texture2D>
#include <osg/Uniform>

namespace fleet
{
    /**
     * The sea at night: one full-screen quad drawn by a fragment shader.
     * Waves move on their own; each encoder is a lighthouse whose beam
     * sweeps the water where its knob points.
     */
    class Water : public osg::Group
    {
    public:
        Water(float width, float height);

        void setTime(float seconds);
        // Where lighthouse `index` stands, in screen pixels.
        void setLighthouse(int index, float x, float y);
        // Where its beam points, in radians, counter-clockwise from +x.
        void setBeam(int index, float radians);
        // How bright its beam is, 0..1.
        void setBeamPower(int index, float power);

        // The light the beams cast, as a low-resolution screen-sized texture:
        // red is the light (over BEAM_RANGE), blue the fog. Everything
        // else in the scene is lit from it.
        osg::Texture2D *beamTexture() const { return _beamTexture.get(); }

    private:
        osg::ref_ptr<osg::Uniform> _time;
        osg::ref_ptr<osg::Uniform> _lighthouses;
        osg::ref_ptr<osg::Uniform> _beams;
        osg::ref_ptr<osg::Uniform> _powers;
        osg::ref_ptr<osg::Texture2D> _beamTexture;
    };
}
