#pragma once

#include <karl/sdk.h>

#include <string>

namespace fleet
{
    // The game assumes the whole display.
    constexpr float DISPLAY_WIDTH = karl::sdk::DISPLAY_WIDTH;
    constexpr float DISPLAY_HEIGHT = karl::sdk::DISPLAY_HEIGHT;

    // The game is seen from above and a little to the south: a point at height
    // z is drawn this much of z higher up the screen. Shared by the models'
    // shader and the particles, so a cannonball lands where its shadow is.
    constexpr float OBLIQUE = 0.6f;

    // Direction towards the sun, matching the water shader's. Unnormalised.
    constexpr float SUN_X = -0.45f;
    constexpr float SUN_Y = 0.55f;
    constexpr float SUN_Z = 0.7f;

    // The canal: the only water in canal mode, in screen pixels (y up). Its
    // south bank runs just north of the third knob row's lighthouses.
    constexpr float CANAL_X0 = 70.0f;
    constexpr float CANAL_Y0 = 350.0f;
    constexpr float CANAL_X1 = 1200.0f;
    constexpr float CANAL_Y1 = 450.0f;

    // Night lighting, shared by every shader. The beam texture holds the
    // lighthouses' light divided by BEAM_RANGE, since crossing beams add up
    // past the 0..1 a texture keeps. MOON is the cold light everything gets
    // at night and DAYLIGHT by day; WARM is the lamps'. FOG and MIST are the
    // colour of fog no beam is crossing, by night and by day.
    inline const char *NIGHT_DEFINES = R"(
#define BEAM_RANGE 2.0
#define MOON 0.055, 0.065, 0.12
#define DAYLIGHT 0.95, 0.93, 0.88
#define WARM 1.0, 0.86, 0.55
#define FOG 0.025, 0.032, 0.048
#define MIST 0.62, 0.68, 0.74
)";

    // Light and fog at a point on the water, for shaders that are handed the
    // beam texture as uLight and the screen size as uScreen. The texture's
    // blue channel is the fog's density.
    inline const char *NIGHT_LIGHT = R"(
uniform sampler2D uLight;
uniform vec2 uScreen;
// 0 night, 1 day: the panel's DAY / NIGHT buttons, faded between.
uniform float uDay;
vec3 ambientLight()
{
    return mix(vec3(MOON), vec3(DAYLIGHT), uDay);
}
// Where there is water: x0, y0, x1, y1. The whole screen and more at sea.
uniform vec4 uWater;
// 1 inside the water, 0 outside, softened over a pixel.
float inWater(vec2 p)
{
    vec2 inside = smoothstep(uWater.xy - 0.5, uWater.xy + 0.5, p)
                * smoothstep(uWater.zw + 0.5, uWater.zw - 0.5, p);
    return inside.x * inside.y;
}
vec4 lightAt(vec2 ground)
{
    return texture2D(uLight, ground / uScreen);
}
// By day the lamps show less, though enough to aim by.
float lampAt(vec4 light)
{
    return (1.0 - exp(-light.r * BEAM_RANGE * 1.3)) * (1.0 - 0.6 * uDay);
}
vec3 nightLight(vec2 ground, float diffuse)
{
    float lit = lampAt(lightAt(ground));
    return ambientLight() * (0.7 + diffuse * 0.6) + vec3(WARM) * lit * (0.55 + diffuse * 0.9);
}
// Fog hides what is behind it, and glows where a beam crosses it.
vec3 fogged(vec3 color, vec2 ground)
{
    vec4 light = lightAt(ground);
    vec3 fog = mix(vec3(FOG), vec3(MIST), uDay) + vec3(WARM) * lampAt(light) * 0.3;
    return mix(color, fog, light.b);
}
)";

    // The texture unit the beam texture sits on outside the water.
    constexpr int LIGHT_UNIT = 1;

    // A shader's source with `extra` inserted after its #version line.
    inline std::string withHeader(const char *source, const std::string &extra)
    {
        std::string text = source;
        const size_t lineEnd = text.find('\n', text.find("#version"));
        text.insert(lineEnd + 1, extra);
        return text;
    }

    // Render bins, back to front. Water and everything under the ships draw
    // without depth; the models sort by their own depth in between.
    constexpr int BIN_WATER = -20;
    constexpr int BIN_UNDER = -10;
    constexpr int BIN_MODELS = 0;
    constexpr int BIN_FX = 10;
    constexpr int BIN_GLOW = 11;
    // The sonar plot: over the world and its fog, under the HUD.
    constexpr int BIN_TACTICAL = 15;
    constexpr int BIN_HUD = 20;
}
