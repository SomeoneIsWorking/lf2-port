/* THE STAGE'S ONE LIGHT, as pure arithmetic.
 *
 * Everything the port lights is lit from a single key direction: the fighters' shading, their
 * cast shadows, and the hand-woven geometry of a stage set (issues #30, #37, #62). The whole
 * arrangement rests on there being ONE vector, and the failure it exists to prevent is
 * geometry lit from the right while every shadow in the picture falls to the left.
 *
 * WHY THIS IS A HEADER AND NOT A FUNCTION IN hd2d.c. That arrangement has been broken once
 * already and silently: runtime/video/mesh.c kept its own copy of the direction, the copy had
 * drifted to about fifteen degrees away from hd2d.c's, and the comment above it asserted the
 * two were the same numbers. Nothing could have caught it, because the light lived inside a
 * file that needs a GPU to run at all -- so there was nowhere to assert anything about it
 * without booting the game and looking at a picture.
 *
 * The arithmetic is now here, where tests/test_stagelight.c walks it in microseconds, and the
 * shipping code INCLUDES this header rather than holding a copy of the formulas. That is the
 * same shape as geom.h, and it is the project's answer to "a claim that can be checked offline
 * must be".
 *
 * THE AXES are the stage's own, which are not a graphics convention:
 *   x   across the stage
 *   y   UP -- LF2's jump axis (claim C018), the direction a fighter rises in
 *   z   toward the camera, along the floor
 */
#ifndef LF2_STAGELIGHT_H
#define LF2_STAGELIGHT_H

#include <math.h>

/* Elevation is clamped well clear of the horizon because cot(elevation) is what stretches a
 * cast shadow: at 0 degrees it is infinite -- a shadow the length of the stage, which is not a
 * look anyone would choose and would read as a bug rather than as a setting. 89 rather than 90
 * keeps the azimuth meaningful; at exactly overhead a shadow has no direction and the azimuth
 * control would appear dead. */
enum { STAGELIGHT_EL_MIN = 12, STAGELIGHT_EL_MAX = 89 };

static inline float stagelight_clamp_elevation(float el)
{
    if (el < (float)STAGELIGHT_EL_MIN) return (float)STAGELIGHT_EL_MIN;
    if (el > (float)STAGELIGHT_EL_MAX) return (float)STAGELIGHT_EL_MAX;
    return el;
}

/* Azimuth wraps rather than clamps -- it is an angle round the fighters and every value is a
 * real direction, so the two ends of the control meet.
 *
 * The range is the CLOSED [-180, 180], not the half-open (-180, 180] an earlier version of this
 * comment claimed: -540 comes back as -180 and +540 as +180. Both name the same direction --
 * straight behind the fighters -- so nothing downstream can tell them apart, and forcing one
 * end onto the other would only make a menu reading flip sign as a player turned past the back
 * of the stage. Written down because the test asserts the real behaviour, and a comment that
 * disagrees with it is how the next reader "fixes" something that was never wrong. */
static inline float stagelight_wrap_azimuth(float az)
{
    while (az < -180.0f) az += 360.0f;
    while (az >  180.0f) az -= 360.0f;
    return az;
}

/* The two angles a player sets, as the one direction vector everything reads.
 *
 * AZIMUTH is degrees around the fighters: 0 puts the light straight in front of them (from the
 * camera's side), and negative swings it to the left. It decides which way a shadow points.
 * ELEVATION is degrees above the horizon; it decides how long a shadow is.
 *
 * The result is a UNIT vector pointing TOWARD the light, so a surface's lambert term is a plain
 * dot product with its normal and needs no further scaling. */
static inline void stagelight_vector(float az_deg, float el_deg, float out[3])
{
    const float az = stagelight_wrap_azimuth(az_deg);
    const float el = stagelight_clamp_elevation(el_deg);
    const float a = az * 3.14159265f / 180.0f, e = el * 3.14159265f / 180.0f;
    out[0] = cosf(e) * sinf(a);
    out[1] = sinf(e);
    out[2] = cosf(e) * cosf(a);
}

/* THE DEFAULT, in the form a player sets it in -- not as a vector. A vector default would be a
 * second spelling of the same fact, and the pair had already drifted once: hd2d.c's initialiser
 * held { -0.25, 0.94, 0.22 } beside angles of (-48.7, 70), which produce
 * (-0.2569, 0.9397, 0.2257). Rounded, so nobody would ever notice it was a copy rather than a
 * derivation, and nothing would notice if it stopped agreeing. */
#define STAGELIGHT_AZ_DEFAULT (-48.7f)
#define STAGELIGHT_EL_DEFAULT ( 70.0f)

/* Where a point at height h above the floor casts its shadow, per unit of height, in the
 * stage's own axes: h * (-Lx/Ly, -Lz/Ly).
 *
 * `up` is a screen-Y displacement UP the picture rather than a third world axis, because LF2's
 * z projects straight down the screen at slope 1 (claim C018) -- so a shadow's z offset and its
 * screen offset are the same number. That is the whole reason a 2D shear can stand in for a
 * projection here.
 *
 * Dividing by Ly is what makes a low light throw a long shadow and a high one throw a short
 * one, and it is the SAME vector the shading uses, which is what stops the two disagreeing.
 * Ly is floored because a caller may hand in a vector that did not come from
 * stagelight_vector -- the clamp above already keeps a real one well clear of zero. */
static inline void stagelight_shadow(const float dir[3], float *across, float *up)
{
    const float y = dir[1] < 0.05f ? 0.05f : dir[1];
    *across = -dir[0] / y;
    *up     =  dir[2] / y;
}

#endif
