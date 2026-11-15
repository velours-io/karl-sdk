#include "Water.h"

#include "View.h"

#include <osg/Camera>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>
#include <osg/Texture2D>

#include <cmath>
#include <string>

using namespace fleet;

namespace
{
    // GLSL 1.20: the desktop GL 2.1 context OSG opens on both macOS and the Pi.
    const char *VERTEX = R"(
#version 120
varying vec2 vPos;
void main()
{
    vPos = gl_Vertex.xy;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)";

    // Top-down daylight sea, in the manner of Age of Empires II DE: a teal
    // ground with lighter and darker patches, small sun-lit waves, glints, and
    // a few white caps.
    const char *WATER_FRAGMENT = R"(
#version 120
uniform float uTime;
uniform vec2 uResolution;
varying vec2 vPos;

// Arithmetic only: sin() based hashes cost far more on the Pi's GPU.
float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// One travelling wave: adds its height and its slope.
void wave(vec2 p, vec2 dir, float wavelength, float amp, float speed, float t,
          inout float h, inout vec2 grad)
{
    float k = 6.2831 / wavelength;
    float phase = dot(p, dir) * k + t * speed;
    h += sin(phase) * amp;
    grad += dir * (cos(phase) * amp * k);
}

void main()
{
    vec2 uv = vPos / uResolution;
    float t = uTime;

    // A little noise in the coordinates so the waves never line up in rows.
    vec2 warp = vec2(noise(vPos * 0.008 + t * 0.05), noise(vPos * 0.008 + 17.0 - t * 0.05));
    vec2 p = vPos + (warp - 0.5) * 40.0;

    float h = 0.0;
    vec2 grad = vec2(0.0);
    wave(p, vec2(0.944, 0.330), 140.0, 1.0, 1.1, t, h, grad);
    wave(p, vec2(0.707, -0.707), 95.0, 0.7, 1.4, t, h, grad);
    wave(p, vec2(-0.196, 0.981), 60.0, 0.45, 1.9, t, h, grad);
    wave(p, vec2(0.874, 0.486), 37.0, 0.3, 2.4, t, h, grad);
    wave(p, vec2(-0.6, 0.8), 23.0, 0.18, 3.1, t, h, grad);
    wave(p, vec2(0.287, -0.958), 15.0, 0.1, 3.9, t, h, grad);

    vec3 n = normalize(vec3(-grad * 6.0, 1.0));
    vec3 sun = normalize(vec3(-0.45, 0.55, 0.7));
    float diffuse = clamp(dot(n, sun), 0.0, 1.0);
    vec3 r = reflect(-sun, n);
    float glint = pow(max(r.z, 0.0), 140.0);

    // Broad patches of lighter and deeper water, drifting slowly.
    float patches = noise(vPos * 0.0025 + t * 0.01) * 0.65 + noise(vPos * 0.006 - t * 0.015) * 0.35;
    vec3 deep = vec3(0.04, 0.27, 0.42);
    vec3 light = vec3(0.10, 0.45, 0.58);
    vec3 color = mix(deep, light, smoothstep(0.25, 0.8, patches));

    vec4 beams = lightAt(vPos);

    // White caps: only on the high crests, and only where a slow noise lets
    // them through, so they come and go in a few places at a time.
    // The noise is only paid for on a crest, which is a small part of the screen.
    float crest = smoothstep(1.6, 2.2, h);
    if (crest > 0.0)
    {
        float gate = smoothstep(0.62, 0.8, noise(vPos * 0.012 + vec2(t * 0.08, -t * 0.05)));
        color = mix(color, vec3(0.9, 0.96, 1.0), crest * gate * 0.7);
    }

    // Night: the sea as daylight would show it, lit only by a faint moon and
    // by the beams. Rolled off rather than added up, so where beams cross
    // they glow instead of burning the water white.
    vec3 warm = vec3(WARM);
    float lit = lampAt(beams);
    vec3 moon = ambientLight() * (0.7 + diffuse * 0.6);
    vec3 lamp = warm * lit * (0.55 + diffuse * 0.9);
    // Under the lamps the teal is washed toward grey, or warm light on it
    // reads green.
    vec3 washed = mix(color, vec3(dot(color, vec3(0.3, 0.5, 0.2))), 0.6);
    color = color * moon + washed * lamp;
    color += mix(vec3(0.6, 0.7, 1.0) * 0.08, vec3(0.85, 0.95, 1.0) * 0.3, uDay) * glint;
    color += warm * glint * lit * 0.7;
    // A little haze in the beam itself, so it reads as lamplight rather than
    // as lighter sea.
    color += warm * lit * 0.12;

    color = fogged(color, vPos);

    vec2 v = uv - 0.5;
    color *= 1.0 - dot(v, v) * 0.35;

    gl_FragColor = vec4(color * inWater(vPos), 1.0);
}
)";

    // The lighthouse beams alone, as one brightness, drawn into a small
    // texture the water reads back. They are soft, so a sixteenth of the
    // pixels is enough, and that is what makes 24 of them affordable.
    const char *BEAM_FRAGMENT = R"(
#version 120
#define LIGHTHOUSES 24
// Battle's TOWER_RANGE and ISLAND_BLOCK: what the light reaches is what the
// lighthouse can hit.
#define BEAM_REACH 300.0
#define ISLAND_BLOCK 40.0
uniform vec2 uLighthouse[LIGHTHOUSES];
uniform vec2 uBeamDir[LIGHTHOUSES];
uniform float uBeamPower[LIGHTHOUSES];
uniform float uTime;
// How foggy, 0 clear to 1: the panel's FOG / CLEAR buttons, faded between.
uniform float uFogAmount;
varying vec2 vPos;

float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Banks of fog drifting across from the west, thinning and thickening as
// they go. In the blue channel; cheap here at a sixteenth of the pixels.
float fog(vec2 p, float t)
{
    vec2 q = p * 0.0035 + vec2(-t * 0.018, t * 0.006);
    float n = noise(q) * 0.55 + noise(q * 2.1 + vec2(t * 0.01, 3.7)) * 0.3
            + noise(q * 4.3 - vec2(1.3, t * 0.02)) * 0.15;
    if (uFogAmount <= 0.0)
    {
        return 0.0;
    }
    // More fog: banks start on thinner noise, and are thicker inside.
    float a = uFogAmount;
    return smoothstep(mix(0.75, 0.22, a), mix(0.95, 0.7, a), n) * min(0.97, mix(0.5, 0.96, a));
}

void main()
{
    float light = 0.0;
    for (int k = 0; k < LIGHTHOUSES; k++)
    {
        vec2 d = vPos - uLighthouse[k];
        float dd = dot(d, d);
        float along = dot(d, uBeamDir[k]);
        if (along <= 0.0)
        {
            continue;
        }
        float invDist = inversesqrt(max(dd, 1.0));
        float facing = along * invDist;
        if (facing < 0.94)
        {
            continue;
        }
        float dist = dd * invDist;
        if (dist > BEAM_REACH)
        {
            continue;
        }
        // Other islands stand in the way: the beam is shadowed behind them,
        // which is also where the lighthouse cannot fire (Battle).
        float open = 1.0;
        for (int j = 0; j < LIGHTHOUSES; j++)
        {
            if (j == k)
            {
                continue;
            }
            vec2 toIsland = uLighthouse[j] - uLighthouse[k];
            float t = clamp(dot(toIsland, d) / max(dd, 1.0), 0.0, 1.0);
            float miss = length(toIsland - d * t);
            open *= smoothstep(ISLAND_BLOCK * 0.6, ISLAND_BLOCK, miss);
        }
        // Narrow core inside a wider soft cone, fading out with distance and
        // gone at the reach.
        float wide = smoothstep(0.94, 1.0, facing);
        float core = smoothstep(0.99, 1.0, facing);
        float cone = wide * wide * 0.6 + core * core * 1.2;
        float fade = exp(-dist / 170.0) * smoothstep(BEAM_REACH, BEAM_REACH * 0.7, dist);
        light += cone * fade * open * uBeamPower[k];
    }
    gl_FragColor = vec4(light / BEAM_RANGE, 0.0, fog(vPos, uTime), 1.0);
}
)";

    constexpr int LIGHTHOUSES = karl::sdk::ENCODERS;
    // The beam texture is the screen divided by this on each side.
    constexpr int BEAM_DOWNSCALE = 4;

    osg::ref_ptr<osg::Geometry> screenQuad(float width, float height)
    {
        return osg::createTexturedQuadGeometry(
            osg::Vec3(0, 0, 0), osg::Vec3(width, 0, 0), osg::Vec3(0, height, 0));
    }

    osg::ref_ptr<osg::Program> program(const char *fragment)
    {
        osg::ref_ptr<osg::Program> program = new osg::Program();
        program->addShader(new osg::Shader(osg::Shader::VERTEX, VERTEX));
        program->addShader(new osg::Shader(osg::Shader::FRAGMENT,
                                           withHeader(fragment, std::string(NIGHT_DEFINES) + NIGHT_LIGHT)));
        return program;
    }
}

Water::Water(float width, float height)
    : osg::Group()
{
    _time = new osg::Uniform("uTime", 0.0f);
    _lighthouses = new osg::Uniform(osg::Uniform::FLOAT_VEC2, "uLighthouse", LIGHTHOUSES);
    _beams = new osg::Uniform(osg::Uniform::FLOAT_VEC2, "uBeamDir", LIGHTHOUSES);
    _powers = new osg::Uniform(osg::Uniform::FLOAT, "uBeamPower", LIGHTHOUSES);
    // Parked far off-screen until placed, so an unset one lights nothing.
    for (int i = 0; i < LIGHTHOUSES; i++)
    {
        _lighthouses->setElement(i, osg::Vec2(-10000.0f, -10000.0f));
        _beams->setElement(i, osg::Vec2(1.0f, 0.0f));
        _powers->setElement(i, 1.0f);
    }

    const int beamWidth = static_cast<int>(width) / BEAM_DOWNSCALE;
    const int beamHeight = static_cast<int>(height) / BEAM_DOWNSCALE;

    _beamTexture = new osg::Texture2D();
    osg::Texture2D *beamTexture = _beamTexture.get();
    beamTexture->setTextureSize(beamWidth, beamHeight);
    beamTexture->setInternalFormat(GL_RGBA);
    beamTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    beamTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    beamTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
    beamTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

    // Renders the beams into the texture before the main scene draws.
    // Absolute, so the screen-pixel coordinates the shader works in hold
    // wherever this element sits.
    osg::ref_ptr<osg::Camera> beamCamera = new osg::Camera();
    beamCamera->setRenderOrder(osg::Camera::PRE_RENDER);
    beamCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    beamCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    beamCamera->setProjectionMatrixAsOrtho2D(0, width, 0, height);
    beamCamera->setViewMatrix(osg::Matrix::identity());
    beamCamera->setViewport(0, 0, beamWidth, beamHeight);
    beamCamera->setClearMask(GL_COLOR_BUFFER_BIT);
    beamCamera->setClearColor(osg::Vec4(0, 0, 0, 1));
    beamCamera->attach(osg::Camera::COLOR_BUFFER, beamTexture);

    osg::ref_ptr<osg::Geometry> beamQuad = screenQuad(width, height);
    osg::StateSet *beamState = beamQuad->getOrCreateStateSet();
    beamState->setAttributeAndModes(program(BEAM_FRAGMENT), osg::StateAttribute::ON);
    beamState->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    beamState->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    beamState->setMode(GL_BLEND, osg::StateAttribute::OFF);
    beamState->addUniform(_lighthouses);
    beamState->addUniform(_beams);
    beamState->addUniform(_powers);
    beamState->addUniform(_time);

    osg::ref_ptr<osg::Geode> beamGeode = new osg::Geode();
    beamGeode->addDrawable(beamQuad);
    beamCamera->addChild(beamGeode);
    this->addChild(beamCamera);

    osg::ref_ptr<osg::Geometry> waterQuad = screenQuad(width, height);
    osg::StateSet *waterState = waterQuad->getOrCreateStateSet();
    waterState->setAttributeAndModes(program(WATER_FRAGMENT), osg::StateAttribute::ON);
    waterState->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    // Always the floor: drawn first, and writes no depth for the models to fight.
    waterState->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
    waterState->setRenderBinDetails(BIN_WATER, "RenderBin");
    waterState->setTextureAttributeAndModes(0, beamTexture, osg::StateAttribute::ON);
    waterState->addUniform(new osg::Uniform("uLight", 0));
    waterState->addUniform(new osg::Uniform("uScreen", osg::Vec2(width, height)));
    waterState->addUniform(_time);
    waterState->addUniform(new osg::Uniform("uResolution", osg::Vec2(width, height)));

    osg::ref_ptr<osg::Geode> waterGeode = new osg::Geode();
    waterGeode->addDrawable(waterQuad);
    this->addChild(waterGeode);
}

void Water::setTime(float seconds)
{
    _time->set(seconds);
}

void Water::setLighthouse(int index, float x, float y)
{
    _lighthouses->setElement(index, osg::Vec2(x, y));
}

void Water::setBeam(int index, float radians)
{
    // As a direction rather than an angle: one cos/sin here, not one per pixel.
    _beams->setElement(index, osg::Vec2(std::cos(radians), std::sin(radians)));
}

void Water::setBeamPower(int index, float power)
{
    _powers->setElement(index, power);
}
