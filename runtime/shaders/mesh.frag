/* The hand-woven stage geometry, lit by the SAME key light the sprites are (issue #62).
 *
 * WHY IT SHARES A LIGHT RATHER THAN HAVING ITS OWN. hd2d_light.frag lights the fighters from a
 * direction in the stage's three axes, and hd2d_shadow.frag shears their cast shadows along
 * that same vector -- one constant, so a fighter's shading and their shadow can never disagree.
 * A set lit from somewhere else would put the whole scene into that same contradiction, one
 * step larger: the wall's bright side would face away from the direction the fighter standing
 * against it is lit from. So u_light comes from the port, from the same place the sprite pass
 * takes it.
 *
 * WHAT IS DELIBERATELY SIMPLE HERE, and why that is not a placeholder. The geometry is
 * hand-authored and carries its own vertex colour, which is the artist's decision about what
 * the surface IS; this shader's job is only to give it a side facing the light and a side away
 * from it. A material system, textures and specular are all things to add when a stage asks for
 * them -- adding them first would be the mistake issue #30 records, where five screen-wide
 * effects shipped together and read as a filter over a screenshot rather than as a remaster.
 *
 * The ambient term is a HEMISPHERE rather than a constant, matching hd2d_light.frag: the sky
 * lights an upward-facing surface and the ground lights a downward-facing one, which is what
 * stops the unlit side of a solid going flat black in a way nothing in LF2's own art does.
 */
#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;

layout(set = 3, binding = 0) uniform Light {
    vec4 u_light;      /* xyz: direction TOWARD the light, stage axes. w: its strength. */
    vec4 u_sky;        /* rgb: hemisphere ambient from above. a: unused. */
    vec4 u_ground;     /* rgb: hemisphere ambient from below. a: unused. */
} lit;

layout(location = 0) out vec4 o_color;

void main()
{
    /* Normalised here rather than trusted from the vertex stage: interpolation across a
     * triangle shortens a normal, and a shortened normal quietly darkens the middle of every
     * face. That reads as a lighting choice rather than as the bug it is. */
    vec3 n = normalize(v_normal);
    vec3 l = normalize(lit.u_light.xyz);

    float key = max(dot(n, l), 0.0) * lit.u_light.w;

    /* Hemisphere ambient: 1 straight up, 0 straight down, and the two colours mixed by it. */
    float up = n.y * 0.5 + 0.5;
    vec3 ambient = mix(lit.u_ground.rgb, lit.u_sky.rgb, up);

    o_color = vec4(v_color.rgb * (ambient + key), v_color.a);
}
