/* The hand-woven stage geometry, projected into the SAME picture the game's own layers land in.
 *
 * WHAT THIS IS FOR (issue #62). LF2's stages are flat painted layers, so the HD2D pass has
 * nothing with real depth in it to light. This is the vertex half of the pass that draws
 * authored geometry BEHIND the sprites -- a set for the fighters to stand in.
 *
 * THE PROJECTION IS THE GAME'S, DERIVED AND NOT CHOSEN, and it is NOT a matrix. Three recorded
 * facts settle it:
 *
 *   C031  a layer at parallax depth d shifts by camera/d -- the horizontal is a 1/z translation
 *   C018  a fighter's z is used DIRECTLY as a screen row, so the depth axis projects down the
 *         screen at slope exactly 1, and jump height subtracts from it
 *   ----  but every object shifts by the camera FLAT whatever its z: the game gives a fighter
 *         at the near zboundary and one at the far the SAME parallax rate
 *
 * The last is why no perspective camera can reproduce this, and why LF2 magnifies by exactly 1
 * at every depth -- it draws every layer's picture at its authored size however far away it is.
 * And `screen_x = X - camera/d` is not a linear function of (X, d, 1), so a 4x4 matrix with a
 * perspective divide cannot express it either: it gives X/d, not X - c/d. The depth therefore
 * rides as a per-vertex attribute and the division happens here, per vertex.
 *
 * THIS IS A TRANSCRIPTION of geom_stage_clip in runtime/overrides/geom.h, which is the copy
 * under test -- tests/test_geom.c walks it against the game's own geom_layer_offset on real
 * layers of real stages at five camera positions. Change one and change the other; the C side
 * is the one with the test.
 *
 * DEPTH IS REAL HERE, which is the reason the pass exists rather than being more quads in the
 * display list. The display list is painter-sorted, correct for LF2's sprites and wrong for a
 * mesh, which interpenetrates itself. gl_Position.z carries into a D32_FLOAT attachment.
 */
#version 450

/* FOUR numbers, not three -- x, jump height, floor row and parallax depth are independent in
 * LF2 because the game never unified them. See runtime/video/mesh.h. */
layout(location = 0) in vec4 a_stage;    /* x, jump, row, depth */
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec4 a_color;

layout(set = 1, binding = 0) uniform Camera {
    vec4 u_cam;      /* x: the draw-time camera. yzw: unused. */
    /* WHERE THE COMPOSITION IS ON THE SCREEN, as a scale and a bias per axis.
     *
     * The geometry is authored in the stage's own pixels, and so is the game's picture -- but
     * the picture is then scaled by the window and placed inside it (issue #41), and a wide
     * view shifts the world sideways as well. Every sprite quad already gets that treatment;
     * geometry drawn into the raw output instead would sit at the right place in a 794-wide
     * window and slide out of the stage in any other.
     *
     * So the placement rides here rather than being folded into the view size: it is not a
     * viewport, it is an affine map, and writing it as one is what lets the same shader serve
     * the engine (a placed composition) and the standalone pass (the whole target, scale 1). */
    vec4 u_place;    /* x: x scale. y: x bias. z: y scale. w: y bias. */
    /* THE DEPTH SLIVER this geometry occupies (issue #64), as [lo, hi].
     *
     * The engine draws sprites and geometry into ONE depth buffer, and the two arrive on
     * different scales: a sprite has no depth of its own, only a position in the game's
     * painter order, while a solid has a real parallax depth. They are reconciled by giving
     * each recorded piece of geometry the SLIVER of depth between the two list positions it
     * sits between -- so a solid is ordered against the game's layers by where the port put
     * it in the list, and against OTHER GEOMETRY IN THE SAME SLIVER by its own depth.
     *
     * That is what a set actually needs: interpenetration is a mesh-against-mesh problem, and
     * two solids the port placed in the same gap are exactly the pair that can interpenetrate.
     * Sliver x is lo, y is hi; zw unused. A degenerate [d, d] flattens the geometry onto one
     * plane and is what the old per-gap render targets effectively did. */
    vec4 u_slice;
} cam;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec2 v_uv;

void main()
{
    float depth = a_stage.w;

    /* A depth of 0 means UNKNOWN -- geom_layer_depth's answer for a stage that cannot pan, and
     * for a layer that never moves. It is infinitely far: no camera shift at all. Reading it as
     * the fighters' plane would pan every stage's sky with the fight. */
    float shift = (depth > 0.0) ? cam.u_cam.x / depth : 0.0;

    float sx = a_stage.x - shift;
    float sy = a_stage.z - a_stage.y;          /* the floor row, less the jump height */

    /* depth/(depth+1) rather than a near/far pair: the shipped stages run from 0.89 (The Great
     * Wall's road3, in FRONT of the fighters) to 535 (Forbidden Tower's sky), which no fixed
     * planes cover without spending most of the buffer on emptiness. The fighters' plane lands
     * at exactly 0.5. Unknown depth goes to the far plane. */
    float cz = (depth > 0.0) ? depth / (depth + 1.0) : 1.0;
    /* Into this piece's sliver of the shared buffer. The mapping is monotonic, so the ordering
     * WITHIN the geometry is untouched -- only its range moves. */
    cz = cam.u_slice.x + cz * (cam.u_slice.y - cam.u_slice.x);

    /* The normal is passed through UNTRANSFORMED because the projection above is not a rotation
     * and there is no per-object model matrix: the geometry is authored in stage space. When
     * there is one, it belongs here with its own inverse-transpose rather than reusing the
     * projection, which would shear the normals. */
    v_normal = a_normal;
    v_color  = a_color;
    v_uv     = a_uv;
    gl_Position = vec4(sx * cam.u_place.x + cam.u_place.y,
                       sy * cam.u_place.z + cam.u_place.w,
                       cz, 1.0);
}
