#include "Models.h"

#include "View.h"

#include <osg/Geometry>
#include <osg/Program>
#include <osg/Shader>

#include <cmath>
#include <vector>

using namespace fleet;

namespace
{
    constexpr float PI = 3.14159265f;

    // Vertex colour alpha carries how much the sun shades a face: 1 is fully
    // lit, 0 glows at its own colour - the lighthouse lantern.
    const char *VERTEX = R"(
#version 120
#define OBLIQUE %OBLIQUE%
uniform mat4 osg_ViewMatrix;
uniform mat4 osg_ViewMatrixInverse;
varying vec4 vColor;
varying vec3 vNormal;
varying float vHeight;
varying vec2 vGround;
void main()
{
    vec4 world = osg_ViewMatrixInverse * (gl_ModelViewMatrix * gl_Vertex);
    vec3 n = normalize(mat3(osg_ViewMatrixInverse) * (gl_NormalMatrix * gl_Normal));
    // Two-sided: a face turned away from the viewer is lit from its far side.
    if (dot(n, vec3(0.0, -OBLIQUE, 1.0)) < 0.0)
    {
        n = -n;
    }
    vNormal = n;
    vColor = gl_Color;
    vHeight = world.z;
    vGround = world.xy;

    vec4 lifted = vec4(world.x, world.y + world.z * OBLIQUE, 0.0, 1.0);
    gl_Position = gl_ProjectionMatrix * (osg_ViewMatrix * lifted);
    // Nearer is further down the screen and higher up: that is what hides
    // what along the oblique line of sight.
    gl_Position.z = clamp((world.y - world.z * 2.0) / 1400.0, -0.98, 0.98);
}
)";

    const char *FRAGMENT = R"(
#version 120
uniform float uFlash;
varying vec4 vColor;
varying vec3 vNormal;
varying float vHeight;
varying vec2 vGround;
void main()
{
    // Under the sea, or past the canal's banks.
    if (vHeight < -5.0 || inWater(vGround) < 0.5)
    {
        discard;
    }
    vec3 sun = normalize(vec3(%SUN%));
    // Wrapped, so faces side-on to the light - sails, mostly - are not left dark.
    float diffuse = clamp((dot(normalize(vNormal), sun) + 0.45) / 1.45, 0.0, 1.0);
    // Lit where the beams fall on it, a moonlit shape everywhere else.
    // Anything glowing (alpha 0) keeps its own colour.
    vec3 color = mix(vColor.rgb * 1.15, vColor.rgb * nightLight(vGround, diffuse), vColor.a);
    // Just under the surface the sea shows through.
    float under = smoothstep(0.0, -5.0, vHeight);
    color = mix(color, vec3(0.01, 0.05, 0.09), under * 0.8);
    // A glowing lantern shows through the fog more than lit wood does.
    color = mix(fogged(color, vGround), color, (1.0 - vColor.a) * 0.7);
    color = mix(color, vec3(1.0, 0.95, 0.85), uFlash);
    gl_FragColor = vec4(color, 1.0);
}
)";

    std::string replaceAll(std::string text, const std::string &key, const std::string &value)
    {
        for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size()))
        {
            text.replace(at, key.size(), value);
        }
        return text;
    }

    // Accumulates flat-shaded triangles: every face gets its own normal, the
    // low-poly look, and its own colour.
    struct Mesh
    {
        osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array();
        osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array();
        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array();

        void tri(const osg::Vec3 &a, const osg::Vec3 &b, const osg::Vec3 &c, const osg::Vec4 &color)
        {
            osg::Vec3 n = (b - a) ^ (c - a);
            if (n.length2() < 1e-8f)
            {
                return;
            }
            n.normalize();
            for (const osg::Vec3 &v : {a, b, c})
            {
                vertices->push_back(v);
                normals->push_back(n);
                colors->push_back(color);
            }
        }

        void quad(const osg::Vec3 &a, const osg::Vec3 &b, const osg::Vec3 &c, const osg::Vec3 &d,
                  const osg::Vec4 &color)
        {
            tri(a, b, c, color);
            tri(a, c, d, color);
        }

        // An upright prism of `sides` around (x, y), from z0 to z1, narrowing
        // from r0 to r1. Capped on top.
        void prism(float x, float y, float z0, float z1, float r0, float r1, int sides,
                   const osg::Vec4 &color, const osg::Vec4 &top)
        {
            for (int i = 0; i < sides; i++)
            {
                const float a0 = 2 * PI * i / sides;
                const float a1 = 2 * PI * (i + 1) / sides;
                const osg::Vec3 b0(x + std::cos(a0) * r0, y + std::sin(a0) * r0, z0);
                const osg::Vec3 b1(x + std::cos(a1) * r0, y + std::sin(a1) * r0, z0);
                const osg::Vec3 t0(x + std::cos(a0) * r1, y + std::sin(a0) * r1, z1);
                const osg::Vec3 t1(x + std::cos(a1) * r1, y + std::sin(a1) * r1, z1);
                quad(b0, b1, t1, t0, color);
                tri(osg::Vec3(x, y, z1), t0, t1, top);
            }
        }

        // An axis-aligned box.
        void box(const osg::Vec3 &min, const osg::Vec3 &max, const osg::Vec4 &color)
        {
            const float x0 = min.x(), y0 = min.y(), z0 = min.z();
            const float x1 = max.x(), y1 = max.y(), z1 = max.z();
            quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, color);
            quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, color);
            quad({x1, y1, z0}, {x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, color);
            quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, color);
            quad({x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, color);
        }

        void scale(float factor)
        {
            for (osg::Vec3 &v : *vertices)
            {
                v *= factor;
            }
        }

        osg::ref_ptr<osg::Geode> geode() const
        {
            osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry();
            geometry->setVertexArray(vertices);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
            geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, vertices->size()));
            geometry->setUseVertexBufferObjects(true);
            osg::ref_ptr<osg::Geode> geode = new osg::Geode();
            geode->addDrawable(geometry);
            return geode;
        }
    };

    osg::Vec4 rgb(float r, float g, float b, float lit = 1.0f)
    {
        return osg::Vec4(r, g, b, lit);
    }

    const osg::Vec4 HULL_DARK = rgb(0.30, 0.17, 0.09);
    const osg::Vec4 HULL = rgb(0.47, 0.29, 0.15);
    const osg::Vec4 WALE = rgb(0.62, 0.12, 0.08);
    const osg::Vec4 DECK = rgb(0.72, 0.55, 0.34);
    const osg::Vec4 MAST = rgb(0.35, 0.22, 0.12);
    const osg::Vec4 CANVAS = rgb(0.95, 0.92, 0.83);
    const osg::Vec4 ENEMY = rgb(0.78, 0.13, 0.10);

    // Half-width of a hull at t along it, 0 = stern, 1 = bow: full through the
    // middle, a little narrower at the transom, and drawn to a point at the bow.
    float hullWidth(float t)
    {
        if (t < 0.6f)
        {
            return 0.78f + 0.22f * std::sin(t / 0.6f * PI * 0.5f);
        }
        const float bow = (t - 0.6f) / 0.4f;
        return std::pow(std::cos(bow * PI * 0.5f), 0.7f);
    }

    // A hull from stern (x = -length/2) to bow: outer sides in two bands of
    // wood with a painted wale between, a deck sunk inside the rail, and a
    // flat transom. Sheer rises towards both ends.
    void hull(Mesh &m, float length, float width, float height)
    {
        constexpr int STATIONS = 10;
        const float keel = -height * 0.45f;
        auto station = [&](int i, float &x, float &w, float &h)
        {
            const float t = static_cast<float>(i) / STATIONS;
            x = -length * 0.5f + t * length;
            w = width * 0.5f * hullWidth(t);
            const float sheer = (t - 0.45f) * (t - 0.45f) * 1.4f;
            h = height * (1.0f + sheer);
        };

        for (int i = 0; i < STATIONS; i++)
        {
            float x0, w0, h0, x1, w1, h1;
            station(i, x0, w0, h0);
            station(i + 1, x1, w1, h1);
            for (int side : {-1, 1})
            {
                const float s = static_cast<float>(side);
                const osg::Vec3 rail0(x0, s * w0, h0), rail1(x1, s * w1, h1);
                const osg::Vec3 wale0(x0, s * w0 * 0.97f, h0 * 0.7f), wale1(x1, s * w1 * 0.97f, h1 * 0.7f);
                const osg::Vec3 mid0(x0, s * w0 * 0.93f, h0 * 0.55f), mid1(x1, s * w1 * 0.93f, h1 * 0.55f);
                const osg::Vec3 bot0(x0, s * w0 * 0.55f, keel), bot1(x1, s * w1 * 0.55f, keel);
                if (side > 0)
                {
                    m.quad(rail0, rail1, wale1, wale0, HULL);
                    m.quad(wale0, wale1, mid1, mid0, WALE);
                    m.quad(mid0, mid1, bot1, bot0, HULL_DARK);
                }
                else
                {
                    m.quad(rail1, rail0, wale0, wale1, HULL);
                    m.quad(wale1, wale0, mid0, mid1, WALE);
                    m.quad(mid1, mid0, bot0, bot1, HULL_DARK);
                }
                // Inside of the rail, down to the deck.
                const osg::Vec3 in0(x0, s * (w0 - 1.6f), h0 - 2.0f), in1(x1, s * std::max(0.0f, w1 - 1.6f), h1 - 2.0f);
                m.quad(rail0, rail1, in1, in0, HULL_DARK);
            }
            const osg::Vec3 dl0(x0, -(w0 - 1.6f), h0 - 2.0f), dl1(x1, -std::max(0.0f, w1 - 1.6f), h1 - 2.0f);
            const osg::Vec3 dr0(x0, w0 - 1.6f, h0 - 2.0f), dr1(x1, std::max(0.0f, w1 - 1.6f), h1 - 2.0f);
            m.quad(dl0, dl1, dr1, dr0, DECK);
        }

        // Transom.
        float x, w, h;
        station(0, x, w, h);
        m.quad({x, w, h}, {x, -w, h}, {x, -w * 0.55f, keel}, {x, w * 0.55f, keel}, HULL_DARK);
    }

    void mast(Mesh &m, float x, float deck, float height)
    {
        m.prism(x, 0, deck, deck + height, 1.6f, 1.1f, 6, MAST, MAST);
    }

    // How far the yards are braced round to the wind. Also what keeps a
    // square sail from being seen edge-on, as a line, from above.
    constexpr float BRACE = 0.55f;

    // A square sail across the ship, billowing forward, with a red band.
    void squareSail(Mesh &m, float x, float span, float bottom, float top, float billow)
    {
        constexpr int U = 5, V = 4;
        const float c = std::cos(BRACE), s = std::sin(BRACE);
        auto braced = [&](const osg::Vec3 &p)
        {
            const float dx = p.x() - x;
            return osg::Vec3(x + dx * c - p.y() * s, dx * s + p.y() * c, p.z());
        };
        auto at = [&](int u, int v)
        {
            const float fu = static_cast<float>(u) / U;
            const float fv = static_cast<float>(v) / V;
            const float y = (fu - 0.5f) * span * (1.0f - 0.12f * fv);
            const float z = bottom + (top - bottom) * fv;
            const float bulge = billow * (1.0f - (2 * fu - 1) * (2 * fu - 1)) * std::sin(PI * (0.15f + 0.85f * fv));
            return braced(osg::Vec3(x + bulge, y, z));
        };
        for (int u = 0; u < U; u++)
        {
            for (int v = 0; v < V; v++)
            {
                const osg::Vec4 color = (v == 1 || v == 2) && (u == 2) ? ENEMY : (v == 2 ? ENEMY : CANVAS);
                m.quad(at(u, v), at(u + 1, v), at(u + 1, v + 1), at(u, v + 1), color);
            }
        }
        // The yard it hangs from.
        const osg::Vec3 port = braced({x, -span * 0.5f - 1.5f, top + 0.7f});
        const osg::Vec3 starboard = braced({x, span * 0.5f + 1.5f, top + 0.7f});
        m.quad(port, starboard, starboard - osg::Vec3(0, 0, 1.6f), port - osg::Vec3(0, 0, 1.6f), MAST);
    }

    // A triangular sail along the ship, hung from a slanted yard.
    void lateenSail(Mesh &m, float x, float length, float deck, float height, float billow)
    {
        const osg::Vec3 tack(x + length * 0.55f, 0, deck + height * 0.15f);
        const osg::Vec3 peak(x - length * 0.45f, 0, deck + height);
        const osg::Vec3 clew(x - length * 0.35f, 0, deck + height * 0.12f);
        const osg::Vec3 belly = (tack + peak + clew) / 3.0f + osg::Vec3(0, billow, 0);
        m.tri(tack, peak, belly, CANVAS);
        m.tri(peak, clew, belly, CANVAS);
        m.tri(clew, tack, belly, ENEMY);
        m.box({x - length * 0.5f, -0.6f, deck + height * 0.12f}, {x + length * 0.6f, 0.6f, deck + height * 0.2f}, MAST);
    }

    void pennant(Mesh &m, float x, float z)
    {
        m.tri({x, 0, z}, {x, 0, z - 4}, {x - 9, 1.5f, z - 1.5f}, ENEMY);
    }
}

osg::ref_ptr<osg::StateSet> models::modelState()
{
    const std::string oblique = std::to_string(OBLIQUE);
    const std::string sun = std::to_string(SUN_X) + ", " + std::to_string(SUN_Y) + ", " + std::to_string(SUN_Z);

    osg::ref_ptr<osg::Program> program = new osg::Program();
    program->addShader(new osg::Shader(osg::Shader::VERTEX, replaceAll(VERTEX, "%OBLIQUE%", oblique)));
    program->addShader(new osg::Shader(osg::Shader::FRAGMENT,
                                       withHeader(replaceAll(FRAGMENT, "%SUN%", sun).c_str(),
                                                  std::string(NIGHT_DEFINES) + NIGHT_LIGHT)));

    osg::ref_ptr<osg::StateSet> state = new osg::StateSet();
    state->setAttributeAndModes(program, osg::StateAttribute::ON);
    state->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    state->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);
    state->setMode(GL_BLEND, osg::StateAttribute::OFF);
    state->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
    state->addUniform(new osg::Uniform("uFlash", 0.0f));
    state->setRenderBinDetails(BIN_MODELS, "RenderBin");
    return state;
}

osg::ref_ptr<osg::Geode> models::ship(ShipKind kind)
{
    Mesh m;
    switch (kind)
    {
    case ShipKind::GALLEY:
    {
        // Low and quick: one lateen sail and a row of oars.
        const float length = 44, width = 13, height = 5;
        hull(m, length, width, height);
        // Oars, from the rail down into the water, raked aft.
        for (int i = 0; i < 5; i++)
        {
            const float x = -12 + i * 6.0f;
            for (int side : {-1, 1})
            {
                const float s = static_cast<float>(side);
                const osg::Vec3 grip(x, s * 5.5f, height - 0.5f), blade(x - 3, s * 13.0f, -1.5f);
                m.quad(grip, blade, blade + osg::Vec3(1.2f, 0, 0), grip + osg::Vec3(1.2f, 0, 0), MAST);
            }
        }
        mast(m, 2, height - 2, 30);
        lateenSail(m, 1, 38, height - 2, 30, 5);
        pennant(m, 2, height + 28);
        break;
    }
    case ShipKind::CARAVEL:
    {
        const float length = 58, width = 18, height = 7;
        hull(m, length, width, height);
        // Sterncastle.
        m.box({-29, -7, height - 1}, {-18, 7, height + 5}, HULL);
        m.box({-29, -7, height + 5}, {-18, 7, height + 5.5f}, DECK);
        mast(m, 6, height - 2, 38);
        squareSail(m, 6, 26, height + 10, height + 33, 5);
        mast(m, -12, height + 3, 30);
        lateenSail(m, -12, 24, height + 3, 28, 3);
        pennant(m, 6, height + 36);
        break;
    }
    case ShipKind::GALLEON:
    {
        const float length = 78, width = 24, height = 9;
        hull(m, length, width, height);
        m.box({-39, -10, height - 1}, {-24, 10, height + 7}, HULL);
        m.box({-39, -10, height + 7}, {-24, 10, height + 7.5f}, DECK);
        m.box({22, -7, height - 1}, {31, 7, height + 4}, HULL);
        // A row of gun ports along each side.
        for (int i = 0; i < 6; i++)
        {
            const float x = -18 + i * 7.0f;
            for (int side : {-1, 1})
            {
                const float y = side * 11.9f;
                m.box({x - 1.5f, y - 0.3f, 3.5f}, {x + 1.5f, y + 0.3f, 6.0f}, HULL_DARK);
            }
        }
        mast(m, 12, height - 2, 48);
        squareSail(m, 12, 34, height + 18, height + 44, 6);
        squareSail(m, 12, 30, height + 2, height + 16, 5);
        mast(m, -6, height - 2, 52);
        squareSail(m, -6, 36, height + 20, height + 48, 6);
        squareSail(m, -6, 32, height + 3, height + 18, 5);
        mast(m, -28, height + 6, 32);
        lateenSail(m, -28, 24, height + 6, 30, 3);
        pennant(m, -6, height + 51);
        pennant(m, 12, height + 47);
        break;
    }
    }
    m.scale(SHIP_SCALE);
    return m.geode();
}
