/* The engine's sprite quad (issue #64), and how a magnified sprite is SAMPLED (issue #112).
 *
 * WHY THERE IS NO LIGHTING HERE, when the whole reason for a new engine is to light properly.
 * The engine has to be shown to draw what the renderer it replaces draws before it is allowed
 * to draw anything better: `tools/e2e.py render` diffs the GPU frame against the software
 * compositor to within a level or two of 255, and that comparison is the only reason any of
 * this can be believed. A first version that both replaced the renderer AND changed the shading
 * would fail that test for two reasons at once and could not be told apart from a broken one.
 *
 * So this reproduces the old path exactly: sample, multiply by the vertex colour, out. The
 * lighting moves in afterwards, as its own step, against the same test -- and when it does it
 * will be REAL shading with the depth buffer this engine has, rather than hd2d.c's reconstruction
 * of a normal from the gradient of a finished picture's silhouette.
 *
 * ---- THE SAMPLING CHAIN (issue #112) ----
 *
 * The frame is composed at the window's resolution, so a fighter is magnified roughly twice,
 * and plain NEAREST leaves every silhouette and interior colour edge staircased. A player
 * builds a CHAIN of resampling passes over the art (runtime/video/spritefilter.h owns what a
 * chain may contain and why); this shader evaluates it, per fragment, with no intermediate
 * render targets. Every quad that is not a filtered object sprite carries an empty chain and
 * takes the one-tap path at the top of main(), byte for byte what this engine always drew.
 *
 * THE MODEL. Pass i scales the picture by n_i, so after the chain the art has F = prod(n_i)
 * times its source resolution. The chain does not change the quad, so the fragment's position
 * in the CHAIN IMAGE is simply q = p * F, with p the fragment's position in source texels.
 * Sampling the chain image is defined recursively, from the last pass back to the art:
 *
 *   level 0 (the art)  S(c)      = the texel at floor(c), or transparent outside the sprite
 *   pass i, NEAREST    L_i(c)    = L_{i-1}( (floor(c) + 0.5) / n_i )
 *   pass i, LINEAR     L_i(c)    = premultiplied bilinear of L_{i-1} around (floor(c)+0.5)/n_i
 *
 * The floor() in every arm is what makes a pass a PASS: it quantises to that pass's own pixel
 * grid, which is why `nearest:1/2` genuinely coarsens the art rather than being cancelled by a
 * later magnification. And the recursion is why a LINEAR pass costs four taps of everything
 * before it -- the cap on how many a chain may hold lives in spritefilter.h with that sum.
 *
 * THE TWO TERMINAL STEPS. `aa` RECONSTRUCTS edges rather than resampling them: a perceptual
 * 3x3 classifier follows shaded contours, infers diagonal/shallow/steep continuation, and
 * blends only inside the resulting edge wedge. Issue #113 is why no filter of the whole chain
 * image -- bilinear, or a box narrowed to a screen pixel -- can do this. `outline`
 * paints the fragments that are transparent but within N chain pixels of something opaque --
 * the silhouette the game itself drew, at whatever resolution the chain left it.
 *
 * MIXING IS PREMULTIPLIED, and it has to be: guest alpha is binary and fill_rgba zeroes RGB
 * under the colour key, so a straight-alpha blend would pull black out of every transparent
 * neighbour and fringe the silhouette.
 *
 * OUTSIDE THE QUAD'S OWN RECT IS TRANSPARENT, not the clamped edge texel. Sprite sheets butt
 * frames edge to edge, so an unclamped tap would blend the NEXT FRAME's art into this one's
 * silhouette; and an outline needs the fragments beyond the art to read as empty rather than
 * as a smear of the border texel (engine.c grows a quad by the outline's width so those
 * fragments exist at all). The cost is that art which touches its own frame border is treated
 * as ending there, which is what the frame says.
 */
#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;

layout(set = 2, binding = 0) uniform sampler2D u_src;

layout(set = 3, binding = 0) uniform Flags
{
    vec4 f_data;    /* x: 1 with a texture. y: pass count. z: aa. w: outline width, chain px. */
    vec4 f_rect;    /* authoritative source rect in sheet texels: xy origin, zw extent. */
    vec4 f_pass[8]; /* per pass: x scale factor (AUTO already resolved), y 0 nearest / 1 linear. */
    vec4 f_outline; /* the outline's colour and alpha. */
    vec4 f_cut;     /* x: the index of the chain's LINEAR pass, or the pass count when it has none. */
}
f;

layout(location = 0) out vec4 o_color;

/* THE CHAIN WORKS IN WHOLE TEXELS OF THE SHEET, not in fractions reconstructed from UVs.
 * Texel-centre endpoints span extent-1 and cannot recover the source bounds (issue #113), so
 * the draw supplies the authoritative origin and extent directly. */
vec2 sheet_size(void) { return vec2(textureSize(u_src, 0)); }
vec2 rect_origin(void) { return f.f_rect.xy; }
vec2 rect_extent(void) { return f.f_rect.zw; }

/* Level 0: the art itself, addressed in source texels from the quad's own frame. */
vec4 tap_source(vec2 c)
{
    const vec2 i = floor(c);
    if (any(lessThan(i, vec2(0.0))) || any(greaterThanEqual(i, rect_extent()))) return vec4(0.0);
    return texture(u_src, (rect_origin() + i + 0.5) / sheet_size());
}

/* Walk a coordinate DOWN through the nearest passes in [lo, hi), last pass first. A nearest
 * pass moves no data -- it only says which pixel of the level below this one is -- so a run of
 * them is a loop over coordinates and costs nothing per tap. That is the whole reason the
 * shader is built around the two LINEAR cut points rather than around the passes. */
vec2 nearest_run(vec2 c, int lo, int hi)
{
    for (int i = hi - 1; i >= lo; i--) c = (floor(c) + 0.5) / f.f_pass[i].x;
    return c;
}

/* Premultiplied bilinear of four samples, returned as straight alpha again. */
vec4 mix_four(vec4 a, vec4 b, vec4 c, vec4 d, vec2 w)
{
    vec4 pa = vec4(a.rgb * a.a, a.a), pb = vec4(b.rgb * b.a, b.a);
    vec4 pc = vec4(c.rgb * c.a, c.a), pd = vec4(d.rgb * d.a, d.a);
    vec4 m = mix(mix(pa, pb, w.x), mix(pc, pd, w.x), w.y);
    return vec4(m.a > 0.0 ? m.rgb / m.a : vec3(0.0), m.a);
}

/* THE TWO LEVELS. `eval_below0` is the art with every nearest pass under the linear one
 * already walked; `eval_linear0` applies that one linear pass as four taps of it. With no
 * linear pass the upper level collapses to the lower and the whole chain is a single tap; with
 * one it is four, or sixteen under `aa`.
 *
 * A SECOND linear pass would be sixty-four, and it is capped away (spritefilter.h) for a
 * reason that is not per-fragment cost alone: the shader ships as a COMMITTED SPIR-V and MSL
 * payload, the optimiser inlines every tap site, and measured on this file the second linear
 * pass takes the SPIR-V from 39 KB to 174 KB -- a megabyte of generated header re-diffed on
 * every future shader edit, to buy a second blur of art that already has one available
 * mid-chain and one at the screen. */
vec4 eval_below0(vec2 c) { return tap_source(nearest_run(c, 0, int(f.f_cut.x))); }

/* c is in the image the first LINEAR pass produced. */
vec4 eval_linear0(vec2 c)
{
    const int cut = int(f.f_cut.x);
    if (float(cut) >= f.f_data.y) return eval_below0(c);
    const vec2 s = (floor(c) + 0.5) / f.f_pass[cut].x;
    const vec2 b = floor(s - 0.5), w = s - 0.5 - b;
    return mix_four(eval_below0(b + 0.5), eval_below0(b + vec2(1.5, 0.5)), eval_below0(b + vec2(0.5, 1.5)),
                    eval_below0(b + vec2(1.5, 1.5)), w);
}

/* The chain image, addressed after every pass. */
vec4 eval_chain(vec2 c)
{
    const int passes = int(f.f_data.y);
    const int cut = int(f.f_cut.x);
    return eval_linear0(nearest_run(c, min(cut + 1, passes), passes));
}

/* Alpha-aware perceptual distance. RGB is compared premultiplied so transparent colour-key
 * residue cannot invent an edge. Luma is weighted more strongly than chroma; alpha is weighted
 * like luma because the silhouette is a first-class contour. This is an original compact
 * edge metric, not copied xBRZ code (xBRZ itself is GPL). */
float colour_distance2(vec4 a, vec4 b)
{
    const vec4 d = vec4(a.rgb * a.a, a.a) - vec4(b.rgb * b.a, b.a);
    const float y = dot(d.rgb, vec3(0.2627, 0.6780, 0.0593));
    const float cb = 0.5 * (d.b - y);
    const float cr = 0.5 * (d.r - y);
    return 4.0 * y * y + cb * cb + cr * cr + 4.0 * d.a * d.a;
}

/* 1 for the same edge region, 0 for a clearly different one, with room for LF2's shaded
 * outlines. Exact byte equality was issue #113's inert classifier: real contours change
 * colour from texel to texel even when they continue in the same direction. */
float alike(vec4 a, vec4 b)
{
    return 1.0 - smoothstep(0.0025, 0.0500, colour_distance2(a, b));
}

float alpha_alike(vec4 a, vec4 b)
{
    return 1.0 - smoothstep(0.05, 0.30, abs(a.a - b.a));
}

float silhouette_corner(vec4 E, vec4 side0, vec4 side1)
{
    return smoothstep(0.30, 0.70, abs(E.a - 0.5 * (side0.a + side1.a)));
}

float region_alike(vec4 a, vec4 b, float silhouette)
{
    return mix(alike(a, b), alpha_alike(a, b), silhouette);
}

vec4 premul_mix(vec4 a, vec4 b, float w)
{
    const vec4 pa = vec4(a.rgb * a.a, a.a), pb = vec4(b.rgb * b.a, b.a);
    const vec4 m = mix(pa, pb, w);
    return vec4(m.a > 0.0 ? m.rgb / m.a : vec3(0.0), m.a);
}

/* Strength of a reconstructed corner. The two outside neighbours must belong together and
 * differ from E; E must continue on at least one inside axis, which preserves isolated
 * one-pixel details. The diagonal support rejects coincidental colour pairs in noisy art. */
float edge_strength(vec4 E, vec4 side0, vec4 side1, vec4 diagonal, vec4 inside0, vec4 inside1,
                    float silhouette)
{
    const float outside_pair = region_alike(side0, side1, silhouette);
    const float separation = 1.0 - max(region_alike(E, side0, silhouette), region_alike(E, side1, silhouette));
    const float outside_run = max(region_alike(diagonal, side0, silhouette),
                                  region_alike(diagonal, side1, silhouette));
    const float inside_run = max(region_alike(E, inside0, silhouette), region_alike(E, inside1, silhouette));
    return smoothstep(0.18, 0.62, outside_pair * separation * outside_run * inside_run);
}

/* Coverage of an edge wedge whose two intercepts are measured from one source-pixel corner.
 * Continuation along an adjacent source texel extends an intercept from one half-cell toward
 * a full cell, reconstructing shallow and steep runs instead of forcing every edge to 45°.
 * `ramp` is one output fragment's footprint in chain pixels, so only the reconstructed line
 * is antialiased; outside the wedge E is returned exactly. */
float edge_coverage(vec2 uv, vec2 extent, vec2 ramp)
{
    const float signed_distance = 1.0 - uv.x / extent.x - uv.y / extent.y;
    const float footprint = ramp.x / extent.x + ramp.y / extent.y;
    return clamp(signed_distance / max(footprint, 1e-6) + 0.5, 0.0, 1.0);
}

/* Edge-only reconstruction over a 3x3 chain neighbourhood. It follows the xBRZ design goal
 * (infer contour direction, preserve interiors) but not its GPL implementation: this rule and
 * its perceptual thresholds are local to LF2. Every non-edge fragment returns E byte-exact. */
vec4 sample_chain(vec2 q, vec2 ramp)
{
    if (f.f_data.z < 0.5) return eval_chain(q);

    const vec2 c = floor(q), uv = q - c;
    const vec4 A = eval_chain(c + vec2(-0.5, -0.5));
    const vec4 B = eval_chain(c + vec2(0.5, -0.5));
    const vec4 C = eval_chain(c + vec2(1.5, -0.5));
    const vec4 D = eval_chain(c + vec2(-0.5, 0.5));
    const vec4 E = eval_chain(c + 0.5);
    const vec4 F = eval_chain(c + vec2(1.5, 0.5));
    const vec4 G = eval_chain(c + vec2(-0.5, 1.5));
    const vec4 H = eval_chain(c + vec2(0.5, 1.5));
    const vec4 I = eval_chain(c + vec2(1.5, 1.5));

    const vec4 tl = premul_mix(B, D, 0.5);
    const vec4 tr = premul_mix(B, F, 0.5);
    const vec4 bl = premul_mix(D, H, 0.5);
    const vec4 br = premul_mix(F, H, 0.5);

    const float a_tl = silhouette_corner(E, B, D);
    const float a_tr = silhouette_corner(E, B, F);
    const float a_bl = silhouette_corner(E, D, H);
    const float a_br = silhouette_corner(E, F, H);
    const float s_tl = edge_strength(E, B, D, A, F, H, a_tl);
    const float s_tr = edge_strength(E, B, F, C, D, H, a_tr);
    const float s_bl = edge_strength(E, D, H, G, B, F, a_bl);
    const float s_br = edge_strength(E, F, H, I, B, D, a_br);

    const vec2 e_tl = vec2(mix(0.5, 0.9, region_alike(C, tl, a_tl)),
                           mix(0.5, 0.9, region_alike(G, tl, a_tl)));
    const vec2 e_tr = vec2(mix(0.5, 0.9, region_alike(A, tr, a_tr)),
                           mix(0.5, 0.9, region_alike(I, tr, a_tr)));
    const vec2 e_bl = vec2(mix(0.5, 0.9, region_alike(I, bl, a_bl)),
                           mix(0.5, 0.9, region_alike(A, bl, a_bl)));
    const vec2 e_br = vec2(mix(0.5, 0.9, region_alike(G, br, a_br)),
                           mix(0.5, 0.9, region_alike(C, br, a_br)));

    const float w_tl = s_tl * edge_coverage(uv, e_tl, ramp);
    const float w_tr = s_tr * edge_coverage(vec2(1.0 - uv.x, uv.y), e_tr, ramp);
    const float w_bl = s_bl * edge_coverage(vec2(uv.x, 1.0 - uv.y), e_bl, ramp);
    const float w_br = s_br * edge_coverage(vec2(1.0) - uv, e_br, ramp);

    const float w_sum = w_tl + w_tr + w_bl + w_br;
    const float w_e = max(1.0 - w_sum, 0.0);
    vec4 m = vec4(E.rgb * E.a, E.a) * w_e;
    m += vec4(tl.rgb * tl.a, tl.a) * w_tl + vec4(tr.rgb * tr.a, tr.a) * w_tr;
    m += vec4(bl.rgb * bl.a, bl.a) * w_bl + vec4(br.rgb * br.a, br.a) * w_br;
    m /= max(w_e + w_sum, 1e-6);
    return vec4(m.a > 0.0 ? m.rgb / m.a : vec3(0.0), m.a);
}

/* Is any chain pixel within `width` of q covered by ART? The square neighbourhood, not a ring:
 * at a width of two a ring misses the diagonal corners that make a silhouette look bitten.
 *
 * This walks EVERY pass as a coordinate map, a linear one included, and takes one tap of the
 * art. That is not an approximation of `sample_chain` -- it is a different question. The
 * outline asks where the game's own silhouette is; the soft edge a linear pass paints around
 * that silhouette is not part of it, and tracing the blur would put the outline outside the
 * shape by however wide the blur happened to be. It also keeps the neighbourhood at one tap
 * per probe instead of sixteen. */
bool outline_here(vec2 q, float width)
{
    const int r = int(width);
    const int passes = int(f.f_data.y);
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (tap_source(nearest_run(q + vec2(float(dx), float(dy)), 0, passes)).a > 0.0) return true;
        }
    return false;
}

void main()
{
    /* Unconditionally, before any branch: a screen pixel's footprint in source texels. The
     * chain scales it by `factor` below. Derivatives are only defined in uniform control flow,
     * and every fragment of the quad needs the same answer here. */
    const vec2 texel_step = fwidth(v_uv) * sheet_size();

    vec4 tex = vec4(1.0);
    if (f.f_data.x >= 0.5) {
        if (f.f_data.y < 0.5 && f.f_data.z < 0.5 && f.f_data.w < 0.5) {
            tex = texture(u_src, v_uv); /* no chain: the one-tap path this engine always drew */
        } else {
            float factor = 1.0;
            for (int i = 0; i < 8; i++)
                if (float(i) < f.f_data.y) factor *= f.f_pass[i].x;
            const vec2 q = (v_uv * sheet_size() - rect_origin()) * factor;
            tex = sample_chain(q, texel_step * factor);
            /* THE OUTLINE GOES UNDER THE SPRITE, not only where the sprite is absent. On a
             * hard-edged chain the two are the same thing -- guest alpha is binary, so every
             * fragment is either opaque art or empty. But `aa` and a linear pass leave a band
             * of PARTIAL alpha at the silhouette, and an outline that only painted fully
             * transparent fragments came out as dashes around a blurred fighter. Compositing
             * it underneath instead leaves the interior untouched (alpha 1 keeps the art
             * exactly) and lets the border show through the soft edge, which is the whole
             * point of asking for an outline on a smoothed sprite. */
            if (f.f_data.w > 0.0 && tex.a < 1.0 && outline_here(q, f.f_data.w))
                tex = vec4(mix(f.f_outline.rgb, tex.rgb, tex.a), max(tex.a, f.f_outline.a));
        }
    }

    /* The one-tap path keeps the texture's alpha untouched and must discard fully transparent
     * texels, not merely blend them away: a blended zero still writes depth, turning every
     * keyed sprite into an invisible rectangle in the completed visibility buffer. The
     * character mask reuses that buffer to reject fighters covered by later weapons. A chain
     * produces partial alphas exactly AT edges, which is the point of filtering them. */
    o_color = mix(vec4(1.0), tex, f.f_data.x) * v_color;
    if (o_color.a <= 0.0) discard;
}
