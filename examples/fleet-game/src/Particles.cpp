#include "Particles.h"

#include "View.h"

#include <osg/BlendFunc>
#include <osg/Program>
#include <osg/Shader>

#include <algorithm>
#include <cmath>

using namespace fleet;

namespace
{
    const char *VERTEX = R"(
#version 120
varying vec4 vColor;
varying vec3 vUv;
varying vec2 vGround;
void main()
{
    vColor = gl_Color;
    vUv = gl_MultiTexCoord0.xyz;
    vGround = gl_Vertex.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

    // vUv.xy runs -1..1 across the quad; vUv.z picks the shape:
    // 0 soft, 1 hard, 2 rect.
    const char *FRAGMENT = R"(
#version 120
// 1 for things lit by the night - smoke, foam, shadows - 0 for things that
// glow or sit over the scene: fire, health bars.
uniform float uLit;
varying vec4 vColor;
varying vec3 vUv;
varying vec2 vGround;
void main()
{
    vec3 rgb = uLit > 0.5 ? fogged(vColor.rgb * nightLight(vGround, 0.6), vGround) : vColor.rgb;
    if (vUv.z > 1.5)
    {
        gl_FragColor = vec4(rgb, vColor.a);
        return;
    }
    float r = length(vUv.xy);
    float edge = vUv.z > 0.5 ? 1.0 - smoothstep(0.8, 1.0, r) : pow(1.0 - clamp(r, 0.0, 1.0), 1.6);
    // Foam and smoke are of the water: none past the canal's banks.
    if (uLit > 0.5)
    {
        edge *= inWater(vGround);
    }
    if (edge <= 0.0)
    {
        discard;
    }
    gl_FragColor = vec4(rgb, vColor.a * edge);
}
)";

    float shapeCode(ParticleShape shape)
    {
        switch (shape)
        {
        case ParticleShape::HARD:
            return 1.0f;
        case ParticleShape::RECT:
            return 2.0f;
        default:
            return 0.0f;
        }
    }
}

ParticleLayer::ParticleLayer(Blend blend, int renderBin, bool lit)
{
    _geometry = new osg::Geometry();
    _geometry->setUseDisplayList(false);
    _geometry->setUseVertexBufferObjects(true);
    _geometry->setDataVariance(osg::Object::DYNAMIC);

    _vertices = new osg::Vec3Array();
    _colors = new osg::Vec4Array();
    _uvs = new osg::Vec3Array();
    _geometry->setVertexArray(_vertices);
    _geometry->setColorArray(_colors, osg::Array::BIND_PER_VERTEX);
    _geometry->setTexCoordArray(0, _uvs);
    _quads = new osg::DrawArrays(osg::PrimitiveSet::QUADS, 0, 0);
    _geometry->addPrimitiveSet(_quads);
    // Bounds move every frame; never cull the layer away.
    _geometry->setCullingActive(false);
    this->setCullingActive(false);

    osg::ref_ptr<osg::Program> program = new osg::Program();
    program->addShader(new osg::Shader(osg::Shader::VERTEX, VERTEX));
    program->addShader(new osg::Shader(osg::Shader::FRAGMENT,
                                       withHeader(FRAGMENT, std::string(NIGHT_DEFINES) + NIGHT_LIGHT)));

    osg::StateSet *state = this->getOrCreateStateSet();
    state->setAttributeAndModes(program, osg::StateAttribute::ON);
    state->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    state->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    state->setMode(GL_BLEND, osg::StateAttribute::ON);
    state->setAttributeAndModes(blend == Blend::ADD
                                    ? new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE)
                                    : new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    state->setRenderBinDetails(renderBin, "RenderBin");
    state->addUniform(new osg::Uniform("uLit", lit ? 1.0f : 0.0f));

    this->addDrawable(_geometry);
}

void ParticleLayer::add(const Particle &particle)
{
    _particles.push_back(particle);
}

void ParticleLayer::step(float dt)
{
    for (Particle &p : _particles)
    {
        p.age += dt;
        p.vel.z() -= p.gravity * dt;
        p.vel *= std::pow(p.drag, dt);
        p.pos += p.vel * dt;
        p.size = std::max(0.0f, p.size + p.grow * dt);
    }
    // A falling droplet or chip ends at the water.
    _particles.erase(std::remove_if(_particles.begin(), _particles.end(),
                                    [](const Particle &p)
                                    { return p.age >= p.life || (p.gravity > 0 && p.pos.z() < 0); }),
                     _particles.end());
}

void ParticleLayer::draw()
{
    _vertices->clear();
    _colors->clear();
    _uvs->clear();

    for (const Particle &p : _particles)
    {
        const float t = std::min(1.0f, p.age / p.life);
        osg::Vec4 color = p.color;
        color.a() = p.color.a() + (p.endAlpha - p.color.a()) * t;

        const osg::Vec3 center(p.pos.x(), p.pos.y() + p.pos.z() * OBLIQUE, 0.0f);
        const float c = std::cos(p.angle);
        const float s = std::sin(p.angle);
        const osg::Vec3 along(c * p.size * p.stretch, s * p.size * p.stretch, 0);
        const osg::Vec3 across(-s * p.size, c * p.size, 0);
        const float shape = shapeCode(p.shape);

        _vertices->push_back(center - along - across);
        _vertices->push_back(center + along - across);
        _vertices->push_back(center + along + across);
        _vertices->push_back(center - along + across);
        _uvs->push_back(osg::Vec3(-1, -1, shape));
        _uvs->push_back(osg::Vec3(1, -1, shape));
        _uvs->push_back(osg::Vec3(1, 1, shape));
        _uvs->push_back(osg::Vec3(-1, 1, shape));
        for (int i = 0; i < 4; i++)
        {
            _colors->push_back(color);
        }
    }

    _quads->setCount(static_cast<GLsizei>(_vertices->size()));
    _vertices->dirty();
    _colors->dirty();
    _uvs->dirty();
    _geometry->dirtyBound();
}
