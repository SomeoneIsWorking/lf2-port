/* The stage's one light, walked offline (issues #37, #62).
 *
 * WHY THIS TEST EXISTS, and it is not a general wish for coverage. The port lights three things
 * from a single key direction -- the fighters' shading, their cast shadows, and a hand-woven
 * stage set -- and the whole arrangement is worth nothing unless it really is one direction.
 * It was not. runtime/video/mesh.c held its own copy of the vector, the copy sat about fifteen
 * degrees from hd2d.c's, and the comment above it said in so many words that the two were the
 * same numbers. A player moving the light through the pause menu moved the fighters and left
 * the set behind, because hd2d.c's is not even a constant.
 *
 * Nothing could have caught that. The light lived inside a file that needs a GPU to run, so the
 * only instrument was a screenshot -- and a set lit fifteen degrees wrong looks like a set.
 *
 * So the arithmetic moved into runtime/video/stagelight.h, the shipping code INCLUDES it, and
 * this walks it. The assertions below are the properties the rest of the renderer relies on,
 * each stated as the relation it is rather than as a number copied out of a run.
 */
#include "video/stagelight.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

static void ok(const char *what, int cond)
{
    checks++;
    if (cond) return;
    failures++;
    printf("  FAIL  %s\n", what);
}

static void eqf(const char *what, float got, float want, float tol)
{
    checks++;
    const float d = got - want;
    if (d > -tol && d < tol) return;
    failures++;
    printf("  FAIL  %s: got %.6f, expected %.6f\n", what, (double)got, (double)want);
}

int main(void)
{
    float v[3], w[3];

    /* ---- THE VECTOR IS A UNIT VECTOR, at every angle ----
     *
     * The shaders take a plain dot product with a surface normal and call the result the
     * lambert term. A direction that was not unit length would scale the whole key light with
     * the elevation -- geometry getting brighter as the light rose, which reads as an exposure
     * bug and not as a wrong vector. Walked across the range rather than sampled once. */
    for (int el = -30; el <= 120; el += 5) {
        for (int az = -400; az <= 400; az += 37) {
            stagelight_vector((float)az, (float)el, v);
            const float len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (len > 0.9999f && len < 1.0001f) continue;
            printf("  FAIL  az %d el %d gives a length of %.6f, not 1\n",
                   az, el, (double)len);
            failures++;
            checks++;
            goto lengths_done;
        }
    }
    ok("the direction is a unit vector at every angle in and outside the range", 1);
lengths_done:

    /* ---- THE LIGHT IS ALWAYS ABOVE THE FLOOR ----
     *
     * y is the jump axis and a negative y is a light shining up from under the stage: every
     * cast shadow would fall behind its fighter and the floor would be unlit. The elevation
     * clamp is what prevents it, and the clamp is the thing being asserted here -- an input
     * BELOW the minimum must still come out above the floor. */
    stagelight_vector(0.0f, -80.0f, v);
    ok("an elevation far below the horizon is clamped above the floor", v[1] > 0.0f);
    stagelight_vector(0.0f, (float)STAGELIGHT_EL_MIN, w);
    eqf("...to exactly the minimum elevation, not to some other safe value", v[1], w[1], 0.0001f);

    /* ---- ELEVATION DECIDES SHADOW LENGTH, and the relation is cot(elevation) ----
     *
     * Stated as the relation rather than as a measured constant: at azimuth 0 the light is
     * straight in front, so the shadow runs purely up the picture by cot(el) per unit of
     * height. A test asserting 0.3639 would still pass if the formula changed to something
     * that happened to give 0.3639 at this one angle. */
    for (int el = STAGELIGHT_EL_MIN; el <= STAGELIGHT_EL_MAX; el += 7) {
        float across, up;
        stagelight_vector(0.0f, (float)el, v);
        stagelight_shadow(v, &across, &up);
        const float e = (float)el * 3.14159265f / 180.0f;
        const float cot = cosf(e) / sinf(e);
        char what[128];
        snprintf(what, sizeof what, "at elevation %d the shadow runs cot(el) up the picture", el);
        eqf(what, up, cot, 0.001f);
        snprintf(what, sizeof what, "...and not at all across, the light being straight ahead");
        eqf(what, across, 0.0f, 0.001f);
    }

    /* A LOWER light throws a LONGER shadow. The direction of the relation is what a player
     * would notice being wrong, and it survives any change of formula that keeps the sense. */
    {
        float lo[3], hi[3], a1, u1, a2, u2;
        stagelight_vector(0.0f, 20.0f, lo);
        stagelight_vector(0.0f, 80.0f, hi);
        stagelight_shadow(lo, &a1, &u1);
        stagelight_shadow(hi, &a2, &u2);
        ok("a low light throws a longer shadow than a high one", u1 > u2);
    }

    /* ---- AZIMUTH DECIDES WHICH WAY THE SHADOW POINTS ----
     *
     * A negative azimuth swings the light to the left, so the shadow falls to the RIGHT: the
     * displacement is -Lx/Ly and Lx is negative there. Getting this sign backwards puts every
     * shadow on the wrong side of every fighter, which is the single most visible thing the
     * light can get wrong -- and it is invisible in any still frame lit symmetrically. */
    {
        float across, up;
        stagelight_vector(-60.0f, 45.0f, v);
        ok("a light swung LEFT of the fighters has a negative x", v[0] < 0.0f);
        stagelight_shadow(v, &across, &up);
        ok("...so the shadow it casts falls to the RIGHT", across > 0.0f);

        stagelight_vector(60.0f, 45.0f, v);
        ok("a light swung RIGHT of the fighters has a positive x", v[0] > 0.0f);
        stagelight_shadow(v, &across, &up);
        ok("...so the shadow it casts falls to the LEFT", across < 0.0f);
    }

    /* ---- THE AZIMUTH WRAPS, and a wrapped angle is the SAME light ----
     *
     * The control is a circle. If +190 did not mean -170 the two ends of it would not meet and
     * the light would jump as a player turned past the back of the stage. */
    {
        stagelight_vector(190.0f, 40.0f, v);
        stagelight_vector(-170.0f, 40.0f, w);
        eqf("+190 degrees is the same light as -170 (x)", v[0], w[0], 0.0001f);
        eqf("...(y)", v[1], w[1], 0.0001f);
        eqf("...(z)", v[2], w[2], 0.0001f);
        eqf("the wrap brings +540 back to +180", stagelight_wrap_azimuth(540.0f), 180.0f, 0.001f);
        /* And -540 comes back to -180, NOT to +180. The range is the closed [-180, 180] and
         * both ends are the same direction -- straight behind the fighters. The header used to
         * claim the half-open (-180, 180], which is what this check was first written against
         * and which failed: the comment was wrong, not the code. Asserting the real behaviour
         * AND that both ends give the same light is what stops someone "fixing" it. */
        eqf("...and -540 back to -180", stagelight_wrap_azimuth(-540.0f), -180.0f, 0.001f);
        stagelight_vector(180.0f, 40.0f, v);
        stagelight_vector(-180.0f, 40.0f, w);
        eqf("both ends of the range are the same light (x)", v[0], w[0], 0.0001f);
        eqf("...(z)", v[2], w[2], 0.0001f);
    }

    /* ---- THE DEFAULT IS THE ANGLES, and the vector hd2d.c used to hold was a ROUNDED COPY ----
     *
     * This is the check that would have caught the drift. hd2d.c's initialiser held
     * { -0.25, 0.94, 0.22 } beside angles of (-48.7, 70) -- rounded to two places, so it looked
     * like a considered constant rather than a stale derivation, and nothing anywhere would
     * have noticed if the angles moved and it did not. The vector is now derived; this asserts
     * the old literal was that derivation, which is why it was so nearly right and so
     * definitely a copy. */
    {
        stagelight_vector(STAGELIGHT_AZ_DEFAULT, STAGELIGHT_EL_DEFAULT, v);
        eqf("the default angles give the x hd2d.c had rounded to -0.25", v[0], -0.2569f, 0.0005f);
        eqf("...the y it had rounded to 0.94", v[1], 0.9397f, 0.0005f);
        eqf("...the z it had rounded to 0.22", v[2], 0.2257f, 0.0005f);
        ok("the default light comes from the LEFT, as every LF2 stage's art is shaded",
           v[0] < 0.0f);
        ok("...and from well above the floor", v[1] > 0.9f);
    }

    /* ---- A DEGENERATE VECTOR IS FLOORED RATHER THAN DIVIDED BY ----
     *
     * stagelight_shadow takes any vector, not only one this header made, so a caller holding a
     * horizontal or zero direction must get a bounded answer rather than an infinity. An
     * infinite shear is a shadow the length of the stage, and NaN is a quad that vanishes --
     * both read as a rendering bug a long way from the light. */
    {
        float across, up;
        const float flat[3] = { 1.0f, 0.0f, 0.0f };
        stagelight_shadow(flat, &across, &up);
        ok("a horizontal light gives a finite shadow shear", across > -1000.0f && across < 1000.0f);
        ok("...and it is not NaN", across == across && up == up);
    }

    /* ---- THE SHADOW QUAD STANDS ON ITS OWN FEET, and the light carries the rest ----
     *
     * The four relations a cast shadow owes the picture, stated as relations. A quad that
     * failed the first would float beside its fighter; one that failed the second would not
     * lengthen as the light dropped; one that failed the third would stay welded under a
     * jumping fighter however high they went (issue #35's shape of bug). */
    {
        float across, up, q[8], r[8];
        stagelight_vector(-48.7f, 70.0f, v);
        stagelight_shadow(v, &across, &up);

        /* Grounded: the foot edge IS the ground point, however the light is angled. */
        stagelight_shadow_quad(across, up, 100.0f, 300.0f, 40.0f, 80.0f, 0.0f, q);
        eqf("a grounded shadow's foot edge sits at the ground point (x)",
            (q[4] + q[6]) * 0.5f, 100.0f, 0.001f);
        eqf("...(y)", q[5], 300.0f, 0.001f);
        eqf("...and is the sprite's own width wide", q[4] - q[6], 40.0f, 0.001f);

        /* The head edge is the foot edge plus the full-height shear. */
        eqf("the head is displaced across by h * across", q[0], q[6] + 80.0f * across, 0.001f);
        eqf("...and up the picture by h * up", q[1], q[7] - 80.0f * up, 0.001f);

        /* Airborne: the WHOLE quad moves by the lift, feet included. */
        stagelight_shadow_quad(across, up, 100.0f, 300.0f, 40.0f, 80.0f, 25.0f, r);
        eqf("a jump carries the foot edge across by lift * across",
            (r[4] + r[6]) * 0.5f, 100.0f + 25.0f * across, 0.001f);
        eqf("...and up by lift * up", r[5], 300.0f - 25.0f * up, 0.001f);
        eqf("...and the head by the same, so the shadow keeps its shape",
            r[1] - q[1], -25.0f * up, 0.001f);
    }

    printf("stage light: %d checks, %d failure(s)\n", checks, failures);
    if (!checks) {
        printf("  FAIL  no checks ran at all, so this says NOTHING\n");
        return 1;
    }
    return failures ? 1 : 0;
}
