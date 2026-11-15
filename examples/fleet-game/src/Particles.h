#pragma once

#include <osg/Geode>
#include <osg/Geometry>

#include <vector>

namespace fleet
{
    // How a particle is drawn.
    enum class ParticleShape
    {
        // A soft blob, fully faded at the rim: smoke, foam, glow, shadows.
        SOFT,
        // A disc with a crisp edge: cannonballs, droplets, debris.
        HARD,
        // A plain rectangle: health bars.
        RECT,
    };

    struct Particle
    {
        // World position: screen pixels, z = height above the water.
        osg::Vec3 pos;
        osg::Vec3 vel;
        // Pulls vel.z down each second. 0 for things that float.
        float gravity = 0.0f;
        // Multiplies vel each second: 1 keeps it, below 1 slows it.
        float drag = 1.0f;
        // Half extents, growing by `grow` per second.
        float size = 4.0f;
        float grow = 0.0f;
        // Length over width, along `angle` (radians).
        float stretch = 1.0f;
        float angle = 0.0f;
        osg::Vec4 color{1, 1, 1, 1};
        // Alpha at the end of life; it fades linearly from color.a.
        float endAlpha = 0.0f;
        float life = 1.0f;
        float age = 0.0f;
        ParticleShape shape = ParticleShape::SOFT;
    };

    /**
     * Every particle of one kind of blending, drawn in one call. The quads are
     * rebuilt on the CPU each frame - a few hundred at most - so the GPU only
     * pays for the pixels they cover.
     */
    class ParticleLayer : public osg::Geode
    {
    public:
        enum class Blend
        {
            ALPHA,
            ADD,
        };

        // `lit`: shaded by the night and the beams, rather than glowing.
        ParticleLayer(Blend blend, int renderBin, bool lit);

        void add(const Particle &particle);
        // Moves everything on by dt seconds and drops what has expired.
        void step(float dt);
        // Rebuilds the quads. Height is drawn as a lift up the screen, the
        // same oblique view the models use.
        void draw();
        void clear() { _particles.clear(); }

    private:
        std::vector<Particle> _particles;
        osg::ref_ptr<osg::Geometry> _geometry;
        osg::ref_ptr<osg::Vec3Array> _vertices;
        osg::ref_ptr<osg::Vec4Array> _colors;
        osg::ref_ptr<osg::Vec3Array> _uvs;
        osg::ref_ptr<osg::DrawArrays> _quads;
    };
}
