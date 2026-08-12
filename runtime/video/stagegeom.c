/* See stagegeom.h and docs/stage-geometry.md. */
#include "stagegeom.h"
#include "geom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small growable vertex array ---------------------------------------------------------- */

typedef struct { MeshVertex *v; int n, cap; } VBuf;

static int vbuf_push(VBuf *b, MeshVertex x)
{
    if (b->n == b->cap) {
        const int cap = b->cap ? b->cap * 2 : 256;
        MeshVertex *p = (MeshVertex *)realloc(b->v, (size_t)cap * sizeof *p);
        if (!p) return 0;
        b->v = p; b->cap = cap;
    }
    b->v[b->n++] = x;
    return 1;
}

/* ---- OBJ ---------------------------------------------------------------------------------- */

typedef struct { float x, y, z; } V3;
typedef struct { float u, v; }    V2;
typedef struct { V3 *v; V2 *t; V3 *n; int nv, nt, nn, cv, ct, cn; } ObjRaw;

static int grow(void **p, int *cap, int need, size_t elem)
{
    if (need <= *cap) return 1;
    int c = *cap ? *cap : 256;
    while (c < need) c *= 2;
    void *q = realloc(*p, (size_t)c * elem);
    if (!q) return 0;
    *p = q; *cap = c;
    return 1;
}

/* One `f` corner: `a`, `a/b`, `a//c` or `a/b/c`, 1-based, negative meaning relative to the end.
 * Returns 0 on an index that names a vertex the file has not defined -- an error, not a skip,
 * because a face pointing at nothing is a broken model and drawing the rest of it would look
 * like a hole someone authored. */
static int corner(const char *s, const ObjRaw *o, int *vi, int *ti, int *ni)
{
    long a = 0, b = 0, c = 0;
    char *end = NULL;
    a = strtol(s, &end, 10);
    if (end == s) return 0;
    if (*end == '/') {
        const char *p = end + 1;
        if (*p != '/') { b = strtol(p, &end, 10); if (end == p) return 0; }
        else end = (char *)p;
        if (*end == '/') { const char *q = end + 1; c = strtol(q, &end, 10); if (end == q) return 0; }
    }
    *vi = (int)(a > 0 ? a - 1 : o->nv + a);
    *ti = b ? (int)(b > 0 ? b - 1 : o->nt + b) : -1;
    *ni = c ? (int)(c > 0 ? c - 1 : o->nn + c) : -1;
    if (*vi < 0 || *vi >= o->nv) return 0;
    if (*ti >= o->nt) return 0;
    if (*ni >= o->nn) return 0;
    return 1;
}

/* Read an OBJ into `out`, applying `at`, `depth` and `tint`. Every line the subset does not
 * cover is COUNTED, because a parser that silently ignores what it cannot match turns a broken
 * model into a clean bill of health. */
static int obj_load(const char *path, VBuf *out, const float at[3], float depth,
                    const float tint[3], int *skipped, char *err, size_t errn)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "%.180s: cannot be opened", path); return 0; }

    ObjRaw o;
    memset(&o, 0, sizeof o);
    char line[1024];
    long lineno = 0;
    int ok = 1;

    while (ok && fgets(line, sizeof line, f)) {
        lineno++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;

        if (p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            if (!grow((void **)&o.v, &o.cv, o.nv + 1, sizeof *o.v)) { ok = 0; break; }
            V3 v = { 0, 0, 0 };
            if (sscanf(p + 1, "%f %f %f", &v.x, &v.y, &v.z) != 3) {
                snprintf(err, errn, "%.180s:%ld: a `v` needs three numbers", path, lineno);
                ok = 0; break;
            }
            o.v[o.nv++] = v;
        } else if (p[0] == 'v' && p[1] == 't') {
            if (!grow((void **)&o.t, &o.ct, o.nt + 1, sizeof *o.t)) { ok = 0; break; }
            V2 t = { 0, 0 };
            if (sscanf(p + 2, "%f %f", &t.u, &t.v) < 2) t.v = 0.0f;
            o.t[o.nt++] = t;
        } else if (p[0] == 'v' && p[1] == 'n') {
            if (!grow((void **)&o.n, &o.cn, o.nn + 1, sizeof *o.n)) { ok = 0; break; }
            V3 n = { 0, 1, 0 };
            if (sscanf(p + 2, "%f %f %f", &n.x, &n.y, &n.z) != 3) {
                snprintf(err, errn, "%.180s:%ld: a `vn` needs three numbers", path, lineno);
                ok = 0; break;
            }
            o.n[o.nn++] = n;
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            /* Up to 8 corners, fanned into triangles. A quad is the common case; anything
             * larger is unusual enough that refusing beyond 8 is better than a silent clip. */
            int vi[8], ti[8], ni[8], nc = 0;
            char *s = p + 1;
            while (nc < 8) {
                while (*s == ' ' || *s == '\t') s++;
                if (*s == 0 || *s == '\n' || *s == '\r') break;
                if (!corner(s, &o, &vi[nc], &ti[nc], &ni[nc])) {
                    snprintf(err, errn, "%.180s:%ld: a face names a vertex the file has not defined",
                             path, lineno);
                    ok = 0; break;
                }
                nc++;
                while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') s++;
            }
            if (!ok) break;
            if (nc < 3) {
                snprintf(err, errn, "%.180s:%ld: a face needs at least three corners", path, lineno);
                ok = 0; break;
            }
            for (int i = 1; i + 1 < nc; i++) {
                const int c3[3] = { 0, i, i + 1 };
                for (int k = 0; k < 3; k++) {
                    const int j = c3[k];
                    MeshVertex mv;
                    memset(&mv, 0, sizeof mv);
                    mv.x    = o.v[vi[j]].x + at[0];
                    mv.jump = o.v[vi[j]].y + at[1];
                    mv.row  = o.v[vi[j]].z + at[2];
                    mv.depth = depth;
                    if (ti[j] >= 0) { mv.u = o.t[ti[j]].u; mv.v = o.t[ti[j]].v; }
                    if (ni[j] >= 0) { mv.nx = o.n[ni[j]].x; mv.ny = o.n[ni[j]].y;
                                      mv.nz = o.n[ni[j]].z; }
                    else            { mv.ny = 1.0f; }
                    mv.r = tint[0]; mv.g = tint[1]; mv.b = tint[2]; mv.a = 1.0f;
                    if (!vbuf_push(out, mv)) { ok = 0; break; }
                }
                if (!ok) break;
            }
        } else {
            (*skipped)++;
        }
    }

    free(o.v); free(o.t); free(o.n);
    fclose(f);
    if (ok && out->n == 0) {
        snprintf(err, errn, "%.180s: parsed with no faces at all", path);
        ok = 0;
    }
    return ok;
}

/* ---- the scene file ----------------------------------------------------------------------- */

static void trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
}

int stagegeom_load(const char *dir, const char *name,
                   StageLayerLookup lookup, void *ctx, StageGeom *g)
{
    memset(g, 0, sizeof *g);

    char path[512];
    snprintf(path, sizeof path, "%s/%s.stage", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 1;              /* ABSENCE IS NOT FAILURE -- see the header */

    VBuf out;
    memset(&out, 0, sizeof out);

    char line[1024];
    long lineno = 0;
    int ok = 1, in_solid = 0;
    char model[400] = { 0 };
    float at[3] = { 0, 0, 0 }, tint[3] = { 1, 1, 1 }, depth = 0.0f;
    int have_depth = 0;

    while (ok && fgets(line, sizeof line, f)) {
        lineno++;
        trim(line);
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == 0) continue;

        if (strncmp(p, "stage:", 6) == 0) {
            continue;              /* documentation for the reader; the filename is the key */
        } else if (strcmp(p, "solid:") == 0) {
            in_solid = 1; model[0] = 0; have_depth = 0; depth = 0.0f;
            at[0] = at[1] = at[2] = 0.0f;
            tint[0] = tint[1] = tint[2] = 1.0f;
        } else if (strcmp(p, "solid_end") == 0) {
            if (!in_solid) {
                snprintf(g->error, sizeof g->error, "%.180s:%ld: solid_end with no solid",
                         path, lineno);
                ok = 0; break;
            }
            if (!model[0] || !have_depth) {
                snprintf(g->error, sizeof g->error,
                         "%.180s:%ld: a solid needs both a model and a depth (this one has %s)",
                         path, lineno,
                         !model[0] ? (have_depth ? "no model" : "neither") : "no depth");
                ok = 0; break;
            }
            char mp[512];
            snprintf(mp, sizeof mp, "%s/%s", dir, model);
            if (!obj_load(mp, &out, at, depth, tint, &g->skipped_lines,
                          g->error, sizeof g->error)) { ok = 0; break; }
            g->solids++;
            in_solid = 0;
        } else if (!in_solid) {
            snprintf(g->error, sizeof g->error, "%.180s:%ld: `%.60s` is outside any solid",
                     path, lineno, p);
            ok = 0; break;
        } else if (strncmp(p, "model:", 6) == 0) {
            const char *q = p + 6;
            while (*q == ' ' || *q == '\t') q++;
            snprintf(model, sizeof model, "%s", q);
        } else if (strncmp(p, "depth:", 6) == 0) {
            const char *q = p + 6;
            while (*q == ' ' || *q == '\t') q++;
            if (strncmp(q, "layer ", 6) == 0) {
                const char *lname = q + 6;
                while (*lname == ' ') lname++;
                int span = 0, sw = 0;
                if (!lookup || !lookup(ctx, lname, &span, &sw)) {
                    snprintf(g->error, sizeof g->error,
                             "%.180s:%ld: this stage has no layer named `%.60s`, so its depth cannot "
                             "be taken from one -- naming a layer that is not there must not "
                             "fall back to a default", path, lineno, lname);
                    ok = 0; break;
                }
                depth = geom_layer_depth(span, sw);
                if (!(depth > 0.0f)) {
                    snprintf(g->error, sizeof g->error,
                             "%.180s:%ld: layer `%.60s` has NO derivable depth (span %d on a %d-wide "
                             "stage means it never moves, i.e. infinitely far) -- a legitimate "
                             "place for a backdrop and a useless one for a solid",
                             path, lineno, lname, span, sw);
                    ok = 0; break;
                }
            } else {
                char *end = NULL;
                depth = strtof(q, &end);
                if (end == q || !(depth > 0.0f)) {
                    snprintf(g->error, sizeof g->error,
                             "%.180s:%ld: `depth: %.60s` is not a positive number or a `layer <file>`",
                             path, lineno, q);
                    ok = 0; break;
                }
            }
            have_depth = 1;
        } else if (strncmp(p, "at:", 3) == 0) {
            if (sscanf(p + 3, "%f %f %f", &at[0], &at[1], &at[2]) != 3) {
                snprintf(g->error, sizeof g->error, "%.180s:%ld: `at:` needs x, jump and row",
                         path, lineno);
                ok = 0; break;
            }
        } else if (strncmp(p, "tint:", 5) == 0) {
            float r = 255, gg = 255, b = 255;
            if (sscanf(p + 5, "%f %f %f", &r, &gg, &b) != 3) {
                snprintf(g->error, sizeof g->error, "%.180s:%ld: `tint:` needs three 0..255 values",
                         path, lineno);
                ok = 0; break;
            }
            tint[0] = r / 255.0f; tint[1] = gg / 255.0f; tint[2] = b / 255.0f;
        } else {
            snprintf(g->error, sizeof g->error, "%.180s:%ld: `%.60s` is not a key this format has",
                     path, lineno, p);
            ok = 0; break;
        }
    }
    fclose(f);

    if (ok && in_solid) {
        snprintf(g->error, sizeof g->error, "%.180s: the file ends inside a solid", path);
        ok = 0;
    }
    if (!ok) { free(out.v); return 0; }

    g->v = out.v;
    g->n = out.n;
    return 1;
}

void stagegeom_free(StageGeom *g)
{
    if (!g) return;
    free(g->v);
    memset(g, 0, sizeof *g);
}
