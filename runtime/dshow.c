/* DirectShow, enough for the game's background-music path.
 *
 * The game builds a filter graph, renders a .wma, and drives it through IMediaControl
 * and IMediaPosition. WMA is decoded by whatever ffmpeg is on PATH (see runtime/dsound.c),
 * so the graph accepts everything
 * and reports a stopped-at-end stream -- the game proceeds without music rather than
 * calling through a null interface, which is what it did when CoCreateInstance failed.
 *
 * Argument counts matter: stdcall COM methods pop their own arguments, and the count
 * differs per method, so each interface carries a table of them. A generic stub that
 * guesses corrupts the guest stack. */
#include "com.h"
#include "hostwin.h"
#include "guest_ops.h"

#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

/* Argument counts including `this`, indexed by vtable slot. */
static const uint8_t N_GRAPH[] = {
    3, 1, 1,                    /* IUnknown */
    3, 2, 2, 3, 4, 2, 2, 1,     /* IFilterGraph */
    3, 2, 3, 4, 2, 1, 1,        /* IGraphBuilder */
};
static const uint8_t N_DISPATCH[7] = { 3, 1, 1, 2, 4, 6, 9 };
static const uint8_t N_MCONTROL[] = {
    3, 1, 1, 2, 4, 6, 9,        /* IDispatch */
    1, 1, 1, 3, 2, 3, 2, 2, 1,  /* Run Pause Stop GetState RenderFile AddSourceFilter ... */
};
static const uint8_t N_MPOSITION[] = {
    3, 1, 1, 2, 4, 6, 9,
    2, 3, 2, 2, 3, 2, 3, 3, 2, 2, 2,
};
static const uint8_t N_BAUDIO[] = {
    3, 1, 1, 2, 4, 6, 9,
    2, 2, 2, 2,
};
static const uint8_t N_MEVENT[] = {
    3, 1, 1, 2, 4, 6, 9,
    2, 5, 3, 2, 2, 4, 4, 2, 2,
};
static const uint8_t N_MSEEK[] = {
    3, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 5, 5, 3, 3, 3, 2, 2,
};

static const uint8_t *nargs_table(int iface, int *count)
{
    switch (iface) {
    case IF_GRAPH:     *count = sizeof N_GRAPH; return N_GRAPH;
    case IF_MCONTROL:  *count = sizeof N_MCONTROL; return N_MCONTROL;
    case IF_MPOSITION: *count = sizeof N_MPOSITION; return N_MPOSITION;
    case IF_BAUDIO:    *count = sizeof N_BAUDIO; return N_BAUDIO;
    case IF_MEVENT:    *count = sizeof N_MEVENT; return N_MEVENT;
    case IF_MSEEK:     *count = sizeof N_MSEEK; return N_MSEEK;
    default:           *count = 7; return N_DISPATCH;
    }
}

static int cur_nargs(void)
{
    int n = 0;
    const uint8_t *t = nargs_table(com_cur_iface, &n);
    return (com_cur_method < n) ? t[com_cur_method] : 1;
}

/* The single object backing every DirectShow interface: the game only ever drives one
 * graph, so all the interface objects share it. */
static uint32_t iface_obj[IF_COUNT];
static int graph_running;

static void note_no_music(void)
{
    static int said;
    if (!said++)
        fprintf(stderr, "note: could not read the music filename from the graph -- "
                        "background music will be silent\n");
}

static void ds_ok(uint32_t self) { (void)self; com_ret(cur_nargs(), DD_OK); }

static void ds_QueryInterface(uint32_t self)
{
    (void)self;
    const uint32_t iid = ARG(1), out = ARG(2);
    const uint32_t d0 = iid ? LD32(iid) : 0;
    int want = -1;
    switch (d0) {
    case 0x56a868a9u: want = IF_GRAPH; break;
    case 0x56a868b1u: want = IF_MCONTROL; break;
    case 0x56a868b2u: want = IF_MPOSITION; break;
    case 0x56a868b3u: want = IF_BAUDIO; break;
    case 0x56a868b6u:                       /* IMediaEvent */
    case 0x56a868c0u: want = IF_MEVENT; break;   /* IMediaEventEx */
    case 0x36b73880u: want = IF_MSEEK; break;
    default: break;
    }
    if (want < 0) { if (out) ST32(out, 0); com_ret(3, E_NOINTERFACE); return; }
    if (!iface_obj[want]) iface_obj[want] = com_create(want, NULL);
    if (out) ST32(out, iface_obj[want]);
    com_ret(3, DD_OK);
}

/* IMediaControl */
static void mc_Run(uint32_t self)  { (void)self; music_start(); graph_running = 1; com_ret(1, DD_OK); }
/* Stop and Pause must silence the music, or a track change layers the new track over the
 * old one -- the graph is the only thing that knows a track is finished with. */
static void mc_Stop(uint32_t self)  { (void)self; music_stop(); graph_running = 0; com_ret(1, DD_OK); }
static void mc_Pause(uint32_t self) { (void)self; music_stop(); graph_running = 0; com_ret(1, DD_OK); }

/* IBasicAudio::put_Volume(long lVolume) */
static void ba_put_Volume(uint32_t self)
{
    (void)self;
    if (getenv("LF2_AUDIO_DEBUG")) {
        static int n;
        if (n++ < 3) fprintf(stderr, "put_Volume(%d) centibels\n", (int32_t)ARG(1));
    }
    music_set_volume((int32_t)ARG(1));
    com_ret(2, DD_OK);
}
static void mc_GetState(uint32_t self)
{
    (void)self;
    if (ARG(2)) ST32(ARG(2), graph_running ? 2u : 0u);   /* State_Running / State_Stopped */
    com_ret(3, DD_OK);
}
/* The filename arrives as UTF-16. A BSTR nominally carries its byte length in the four
 * bytes before the pointer, but the string the game hands us has a zero there -- so the
 * terminator is what to trust, not the prefix. Only ASCII paths occur here, which makes
 * the conversion a narrowing copy. */
static int bstr_to_path(uint32_t bstr, char *out, size_t n)
{
    if (!bstr) return 0;
    size_t i = 0;
    for (; i < n - 1; i++) {
        const uint16_t w = LD16(bstr + (uint32_t)i * 2);
        if (!w) break;
        out[i] = (w < 0x80) ? (char)w : '?';
    }
    out[i] = 0;
    return i > 0;
}

static void render_file(uint32_t bstr)
{
    char path[512];
    if (getenv("LF2_AUDIO_DEBUG")) {
        fprintf(stderr, "RenderFile bstr=%08x len=%u first16=", bstr,
                bstr ? LD32(bstr - 4) : 0u);
        for (int i = 0; bstr && i < 16; i++) fprintf(stderr, "%02x ", LD8(bstr + i));
        fprintf(stderr, "\n");
    }
    if (!bstr_to_path(bstr, path, sizeof path)) { note_no_music(); return; }
    /* The game uses backslashes; everything else here is POSIX. */
    for (char *c = path; *c; c++) if (*c == '\\') *c = '/';
    if (!music_load(path)) return;
    fprintf(stderr, "music: loaded %s\n", path);
}

static void mc_RenderFile(uint32_t self) { (void)self; render_file(ARG(1)); com_ret(2, DD_OK); }

/* IGraphBuilder::RenderFile(file, playlist) takes TWO parameters, unlike
 * IMediaControl::RenderFile(file). Sharing one handler popped four bytes too few and
 * corrupted the guest stack. */
static void gb_RenderFile(uint32_t self) { (void)self; render_file(ARG(1)); com_ret(3, DD_OK); }

/* IMediaPosition: report a zero-length stream sitting at its end, so any
 * wait-for-completion loop terminates instead of spinning forever. */
static void mp_get_double_zero(uint32_t self)
{
    (void)self;
    const uint32_t out = ARG(1);
    if (out) { ST32(out, 0); ST32(out + 4, 0); }
    com_ret(2, DD_OK);
}

/* IMediaSeeking uses 64-bit integers rather than doubles. */
static void ms_get_int64_zero(uint32_t self)
{
    (void)self;
    const uint32_t out = ARG(1);
    if (out) { ST32(out, 0); ST32(out + 4, 0); }
    com_ret(2, DD_OK);
}

static void fill(int iface, const char *name, int n)
{
    ComClass *c = &com_class[iface];
    c->name = name;
    c->nmethods = n;
    for (int i = 0; i < n; i++) c->method[i] = ds_ok;
    c->method[0] = ds_QueryInterface;
}

void dshow_register(void)
{
    fill(IF_GRAPH,     "IGraphBuilder",  (int)sizeof N_GRAPH);
    fill(IF_MCONTROL,  "IMediaControl",  (int)sizeof N_MCONTROL);
    fill(IF_MPOSITION, "IMediaPosition", (int)sizeof N_MPOSITION);
    fill(IF_BAUDIO,    "IBasicAudio",    (int)sizeof N_BAUDIO);
    fill(IF_MEVENT,    "IMediaEvent",    (int)sizeof N_MEVENT);
    fill(IF_MSEEK,     "IMediaSeeking",  (int)sizeof N_MSEEK);

    com_class[IF_GRAPH].method[13] = gb_RenderFile;

    com_class[IF_MCONTROL].method[7]  = mc_Run;
    com_class[IF_MCONTROL].method[8]  = mc_Pause;
    com_class[IF_MCONTROL].method[9]  = mc_Stop;
    com_class[IF_BAUDIO].method[7]    = ba_put_Volume;
    com_class[IF_MCONTROL].method[10] = mc_GetState;
    com_class[IF_MCONTROL].method[11] = mc_RenderFile;

    com_class[IF_MPOSITION].method[7] = mp_get_double_zero;   /* get_Duration */
    com_class[IF_MPOSITION].method[9] = mp_get_double_zero;   /* get_CurrentPosition */
    com_class[IF_MPOSITION].method[10] = mp_get_double_zero;  /* get_StopTime */

    com_class[IF_MSEEK].method[10] = ms_get_int64_zero;       /* GetDuration */
    com_class[IF_MSEEK].method[11] = ms_get_int64_zero;       /* GetStopPosition */
    com_class[IF_MSEEK].method[12] = ms_get_int64_zero;       /* GetCurrentPosition */
}

uint32_t dshow_create_graph(void)
{
    if (!iface_obj[IF_GRAPH]) iface_obj[IF_GRAPH] = com_create(IF_GRAPH, NULL);
    return iface_obj[IF_GRAPH];
}
