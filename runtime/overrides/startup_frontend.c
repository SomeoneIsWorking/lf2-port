#include "startup_init.h"

#include "guest.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FRONTEND_INIT_LATCH = 0x0044d068,
    GRAPHIC_BYTES = 0x1f50,
    GRAPHIC_COUNT = 0x0c,
    GRAPHIC_X = 0x10,
    GRAPHIC_Y = 0x7e0,
    GRAPHIC_W = 0xfb0,
    GRAPHIC_H = 0x1780,
};

typedef struct {
    uint16_t x, y, w, h;
} GuestClip;

typedef struct {
    uint32_t slot;
    uint32_t name;
} GraphicSpec;

static const GraphicSpec FRONTEND_GRAPHICS[] = {
    {0x00451170, 0x004499b0}, /* LF2_CURSOR */
    {0x004511a0, 0x004499a4}, /* MENU_CLIP */
    {0x00451168, 0x00449998}, /* MENU_CLIP2 */
    {0x00451178, 0x0044998c}, /* MENU_CLIP3 */
    {0x0045116c, 0x00449980}, /* MENU_CLIP4 */
    {0x0045117c, 0x00449974}, /* MENU_CLIP5 */
    {0x004511a4, 0x00449968}, /* MENU_CLIP6 */
    {0x00451188, 0x0044995c}, /* MENU_CLIP7 */
    {0x0045118c, 0x00449950}, /* MENU_WAIT */
    {0x0045119c, 0x00449948}, /* SLOGAN */
    {0x00451190, 0x00449940}, /* ENDING */
    {0x00451198, 0x004499b0}, /* LF2_CURSOR */
    {0x00451174, 0x0044993c}, /* CS2 */
    {0x00451164, 0x00449938}, /* CS3 */
    {0x00451184, 0x00449934}, /* CS4 */
    {0x00451194, 0x00449930}, /* CS5 */
    {0x00451180, 0x0044992c}, /* CS6 */
    {0x004511a8, 0x00449924}, /* FRAME */
    {0x0044faf4, 0x0044991c}, /* WORDS0 */
    {0x0044f888, 0x00449914}, /* WORDS1 */
    {0x0044fcbc, 0x0044990c}, /* WORDS2 */
    {0x0044fb68, 0x00449904}, /* WORDS3 */
    {0x0044faf8, 0x004498fc}, /* WORDS4 */
    {0x0044fd80, 0x004498f4}, /* WORDS5 */
};

static const GuestClip SLOGAN[] = {
    {0, 0, 298, 132},
    {299, 0, 298, 132},
    {0, 133, 298, 132},
    {299, 133, 298, 132},
};
static const GuestClip ENDING[] = {
    {0, 0, 402, 66}, {0, 67, 402, 91}, {0, 159, 402, 156}, {0, 316, 419, 169}, {0, 486, 419, 65}, {403, 53, 15, 13},
};
static const GuestClip MENU_CLIP[] = {
    {0, 0, 794, 37},      {0, 41, 496, 80},     {0, 125, 282, 119},  {0, 247, 304, 126},  {0, 376, 363, 123},
    {500, 42, 291, 27},   {500, 72, 291, 27},   {535, 105, 256, 26}, {535, 137, 256, 26}, {535, 168, 256, 29},
    {460, 203, 334, 108}, {460, 313, 334, 108}, {643, 426, 151, 26}, {489, 426, 151, 26}, {490, 462, 304, 38},
    {316, 124, 29, 19},   {316, 144, 29, 19},   {316, 164, 29, 19},  {316, 184, 37, 19},  {316, 204, 70, 19},
    {316, 224, 70, 19},   {316, 244, 110, 19},  {316, 264, 102, 19}, {316, 284, 45, 19},  {316, 304, 37, 19},
};
static const GuestClip MENU_CLIP2[] = {
    {0, 0, 704, 353},
    {0, 354, 494, 23},
    {0, 379, 494, 23},
    {0, 403, 469, 41},
};
static const GuestClip MENU_CLIP3[] = {
    {0, 0, 334, 108},    {0, 116, 305, 213},  {32, 331, 210, 22},  {18, 358, 238, 24},  {0, 385, 275, 23},
    {0, 412, 275, 24},   {42, 466, 188, 23},  {55, 521, 165, 23},  {348, 0, 304, 156},  {407, 183, 126, 21},
    {379, 206, 186, 21}, {355, 231, 235, 21}, {330, 254, 279, 22}, {352, 278, 228, 22}, {416, 304, 111, 19},
    {348, 156, 304, 10}, {364, 455, 429, 6},  {367, 469, 63, 26},  {487, 469, 63, 26},  {607, 469, 63, 26},
    {727, 469, 63, 26},  {285, 458, 63, 21},  {330, 332, 279, 22}, {330, 363, 279, 22}, {328, 401, 132, 41},
    {488, 400, 176, 41}, {692, 124, 29, 40},  {662, 9, 29, 40},    {692, 9, 29, 40},    {722, 9, 29, 40},
    {752, 9, 29, 40},    {662, 66, 29, 40},   {692, 66, 29, 40},   {722, 66, 29, 40},   {752, 66, 29, 40},
    {662, 124, 29, 40},  {722, 124, 29, 40},  {12, 552, 386, 40},  {33, 438, 206, 24},  {630, 180, 29, 19},
    {630, 200, 29, 19},  {630, 220, 29, 19},  {630, 240, 37, 19},  {630, 260, 70, 19},  {630, 280, 70, 19},
    {630, 300, 110, 19}, {630, 320, 102, 19}, {630, 340, 45, 19},  {630, 360, 37, 19},  {0, 493, 277, 25},
    {293, 504, 277, 25}, {405, 532, 277, 25},
};
static const GuestClip MENU_CLIP4[] = {
    {0, 0, 713, 332},     {0, 335, 40, 45},   {46, 335, 235, 111}, {0, 461, 171, 38},  {183, 461, 173, 38},
    {284, 335, 52, 19},   {342, 334, 47, 20}, {284, 356, 59, 24},  {345, 356, 59, 24}, {170, 0, 543, 332},
    {414, 340, 282, 239}, {717, 0, 15, 19},   {717, 21, 15, 19},   {717, 42, 15, 19},  {717, 63, 15, 19},
    {717, 84, 15, 19},    {717, 105, 15, 19}, {717, 126, 15, 19},  {717, 147, 15, 19}, {142, 508, 258, 58},
    {288, 386, 117, 25},  {288, 416, 60, 24}, {0, 383, 40, 45},    {0, 505, 40, 45},   {2, 586, 725, 12},
};
static const GuestClip MENU_CLIP5[] = {
    {0, 0, 282, 181},  {285, 0, 240, 27},   {285, 32, 240, 27},  {0, 204, 422, 88},
    {0, 297, 422, 88}, {469, 62, 324, 155}, {469, 220, 151, 26}, {0, 389, 530, 200},
    {284, 61, 19, 14}, {303, 61, 19, 14},   {284, 76, 19, 14},   {303, 76, 19, 14},
};
static const GuestClip MENU_CLIP6[] = {
    {0, 0, 704, 312},  {0, 360, 439, 23},  {0, 385, 439, 23},  {0, 411, 704, 258},
    {0, 673, 543, 24}, {548, 363, 19, 19}, {572, 363, 19, 19}, {443, 388, 343, 22},
};
static const GuestClip MENU_CLIP7[] = {
    {0, 0, 643, 162},    {0, 163, 643, 34}, {0, 198, 643, 280}, {0, 482, 234, 29},   {256, 482, 88, 29},
    {0, 516, 360, 22},   {648, 5, 62, 13},  {648, 27, 62, 13},  {648, 49, 62, 13},   {648, 71, 62, 13},
    {359, 483, 108, 18}, {648, 93, 62, 13}, {372, 503, 71, 9},  {371, 514, 147, 10}, {371, 525, 147, 10},
};

static uint32_t guest_graphic_create(uint32_t name)
{
    PUSH32(GRAPHIC_BYTES);
    PUSH32(0x00424784);
    lf2_jit_call(0x004450ac);
    R(ESP) += 4;
    const uint32_t graphic = R(EAX);
    if (!graphic) return 0;

    PUSH32(0);
    PUSH32(name);
    PUSH32(0x40);
    R(ECX) = graphic;
    PUSH32(0x004247a5);
    lf2_jit_call(0x0043ee50);
    return R(EAX);
}

static void create_graphics(size_t begin, size_t end)
{
    for (size_t i = begin; i < end; ++i)
        ST32(FRONTEND_GRAPHICS[i].slot, guest_graphic_create(FRONTEND_GRAPHICS[i].name));
}

static void set_clips(uint32_t slot, const GuestClip *clips, size_t count)
{
    const uint32_t graphic = LD32(slot);
    for (size_t i = 0; i < count; ++i) {
        ST32(graphic + GRAPHIC_X + 4 * i, clips[i].x);
        ST32(graphic + GRAPHIC_Y + 4 * i, clips[i].y);
        ST32(graphic + GRAPHIC_W + 4 * i, clips[i].w);
        ST32(graphic + GRAPHIC_H + 4 * i, clips[i].h);
    }
    ST32(graphic + GRAPHIC_COUNT, (uint32_t)count);
}

static void set_direct_clip_tables(void)
{
#define SET_CLIPS(slot, table) set_clips((slot), (table), sizeof(table) / sizeof((table)[0]))
    SET_CLIPS(0x0045119c, SLOGAN);
    SET_CLIPS(0x00451190, ENDING);
    SET_CLIPS(0x004511a0, MENU_CLIP);
    SET_CLIPS(0x00451168, MENU_CLIP2);
    SET_CLIPS(0x00451178, MENU_CLIP3);
    SET_CLIPS(0x0045116c, MENU_CLIP4);
    SET_CLIPS(0x0045117c, MENU_CLIP5);
    SET_CLIPS(0x004511a4, MENU_CLIP6);
    SET_CLIPS(0x00451188, MENU_CLIP7);
#undef SET_CLIPS
}

static void set_word_atlas(uint32_t slot)
{
    const uint32_t graphic = LD32(slot);
    for (uint32_t i = 0; i < 256; ++i) {
        ST32(graphic + GRAPHIC_X + 4 * i, (i & 15u) * 16u);
        ST32(graphic + GRAPHIC_Y + 4 * i, (i & ~15u) + 1u);
        ST32(graphic + GRAPHIC_W + 4 * i, 8);
        ST32(graphic + GRAPHIC_H + 4 * i, 16);
    }
    ST32(graphic + GRAPHIC_COUNT, 256);
}

static void guest_string_write(uint32_t address, size_t capacity, const char *text)
{
    size_t i = 0;
    while (i + 1 < capacity && text[i]) {
        ST8(address + (uint32_t)i, (uint8_t)text[i]);
        ++i;
    }
    ST8(address + (uint32_t)i, 0);
}

static void trim_line(char *line)
{
    size_t length = strlen(line);
    while (length && (line[length - 1] == ' ' || line[length - 1] == '\r' || line[length - 1] == '\n'))
        line[--length] = 0;
}

static void discard_line(FILE *file)
{
    int c;
    while ((c = fgetc(file)) != '\n' && c != EOF) {
    }
}

static void load_control_file(void)
{
    FILE *file = fopen("data/control.txt", "r");
    if (!file) {
        lf2_log_writef(LF2_LOG_INFO, "startup", "startup: required data/control.txt could not be opened\n");
        abort();
    }

    for (uint32_t player = 0; player < 4; ++player)
        for (uint32_t field = 0; field < 11; ++field) {
            unsigned value;
            if (fscanf(file, "%u", &value) != 1) goto malformed;
            ST32(0x0044fb70 + 0x50 * player + 4 * field, value);
        }

    char names[4][11];
    if (fscanf(file, "%10s %10s %10s %10s", names[0], names[1], names[2], names[3]) != 4) goto malformed;
    for (uint32_t player = 0; player < 4; ++player) {
        for (char *p = names[player]; *p; ++p)
            if (*p == '`') *p = ' ';
        guest_string_write(0x0044fcc0 + 11 * player, 11, names[player]);
    }

    unsigned recording, playback;
    if (fscanf(file, "%u %u", &recording, &playback) != 2) goto malformed;
    ST32(0x00450be8, recording);
    ST32(0x00450be4, playback);
    discard_line(file);

    char line[500];
    if (!fgets(line, sizeof line, file)) goto malformed;
    trim_line(line);
    guest_string_write(0x0044fd18, 100, line);
    if (!fgets(line, sizeof line, file)) goto malformed;
    trim_line(line);
    guest_string_write(0x0044f890, 100, line);

    size_t info_length = 0;
    while (fgets(line, sizeof line, file)) {
        const size_t length = strlen(line);
        if (length > 499 - info_length) {
            lf2_log_writef(LF2_LOG_INFO, "startup",
                           "startup: data/control.txt info text exceeds its 499-byte guest buffer\n");
            fclose(file);
            abort();
        }
        for (size_t i = 0; i < length; ++i) ST8(0x0044f900 + (uint32_t)info_length + (uint32_t)i, (uint8_t)line[i]);
        info_length += length;
    }
    if (ferror(file)) goto malformed;
    ST8(0x0044f900 + (uint32_t)info_length, 0);
    fclose(file);
    return;

malformed:
    lf2_log_writef(LF2_LOG_INFO, "startup", "startup: data/control.txt is malformed or unreadable\n");
    fclose(file);
    abort();
}

void startup_frontend_initialise(void)
{
    startup_init_step_begin(STARTUP_INIT_FRONTEND_RESOURCES, "frontend-resources");
    create_graphics(0, 1);
    for (uint32_t i = 0; i < 8; ++i) ST16(0x0044fcc0 + 11 * i, (uint16_t)('1' + i));
    create_graphics(1, 11);
    set_direct_clip_tables();
    create_graphics(11, sizeof FRONTEND_GRAPHICS / sizeof FRONTEND_GRAPHICS[0]);
    static const uint32_t WORD_SLOTS[] = {
        0x0044faf4, 0x0044f888, 0x0044fcbc, 0x0044fb68, 0x0044faf8, 0x0044fd80,
    };
    for (size_t i = 0; i < sizeof WORD_SLOTS / sizeof WORD_SLOTS[0]; ++i) set_word_atlas(WORD_SLOTS[i]);
    startup_init_step_done(STARTUP_INIT_FRONTEND_RESOURCES, "frontend-resources");

    startup_init_step_begin(STARTUP_INIT_FRONTEND_CONTROLS, "frontend-controls");
    load_control_file();
    ST32(FRONTEND_INIT_LATCH, 0);
    startup_init_step_done(STARTUP_INIT_FRONTEND_CONTROLS, "frontend-controls");
}
