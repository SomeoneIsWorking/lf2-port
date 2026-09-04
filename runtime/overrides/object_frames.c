#include "object_parser.h"

#include "guest.h"
#include "jit_executor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    FRAME_COUNT = 400,
    FRAME_STRIDE = 0x178,
    FRAME_ACTIVE = 0x7a4,
    FRAME_NAME = 0x900,
    FRAME_SOUND_PATH = 0x914,
    FRAME_SOUND_INDEX = 0x918,
    FRAME_ITR_COUNT = 0x8cc,
    FRAME_BDY_COUNT = 0x8d0,
    FRAME_ITR_POINTER = 0x8d4,
    FRAME_BDY_POINTER = 0x8d8,
    SOUND_OBJECTS = 0x452948,
    SOUND_NAMES = 0x455638,
    SOUND_COUNT = 0x458438,
    SOUND_ENABLED = 0x44eecc,
};

typedef struct {
    const char *name;
    uint16_t offset;
} Field;

static const Field FRAME_FIELDS[] = {
    {"pic:", 0x7a8},    {"state:", 0x7ac},  {"wait:", 0x7b0},   {"next:", 0x7b4},    {"dvx:", 0x7b8},
    {"dvy:", 0x7bc},    {"dvz:", 0x7c0},    {"hit_a:", 0x7c8},  {"hit_d:", 0x7cc},   {"hit_j:", 0x7d0},
    {"hit_Fa:", 0x7d4}, {"hit_Ua:", 0x7d8}, {"hit_Da:", 0x7dc}, {"hit_Fj:", 0x7e0},  {"hit_Uj:", 0x7e4},
    {"hit_Dj:", 0x7e8}, {"hit_ja:", 0x7ec}, {"mp:", 0x7f0},     {"centerx:", 0x7f4}, {"centery:", 0x7f8},
};

static const Field OPOINT_FIELDS[] = {
    {"kind:", 0x7fc}, {"x:", 0x800},   {"y:", 0x804},   {"action:", 0x808},
    {"dvx:", 0x80c},  {"dvy:", 0x810}, {"oid:", 0x814}, {"facing:", 0x818},
};

static const Field BPOINT_FIELDS[] = {{"x:", 0x824}, {"y:", 0x828}};

static const Field CPOINT_FIELDS[] = {
    {"kind:", 0x82c},        {"x:", 0x830},          {"y:", 0x834},
    {"injury:", 0x838},      {"cover:", 0x83c},      {"vaction:", 0x840},
    {"aaction:", 0x844},     {"jaction:", 0x848},    {"daction:", 0x84c},
    {"throwvx:", 0x850},     {"throwvy:", 0x854},    {"hurtable:", 0x858},
    {"decrease:", 0x85c},    {"dircontrol:", 0x860}, {"taction:", 0x864},
    {"throwinjury:", 0x868}, {"throwvz:", 0x86c},    {"fronthurtact:", 0x838},
    {"backhurtact:", 0x83c},
};

static const Field WPOINT_FIELDS[] = {
    {"kind:", 0x87c},  {"x:", 0x880},   {"y:", 0x884},   {"weaponact:", 0x888}, {"attacking:", 0x88c},
    {"cover:", 0x890}, {"dvx:", 0x894}, {"dvy:", 0x898}, {"dvz:", 0x89c},
};

static const Field ITR_FIELDS[] = {
    {"kind:", 0},        {"x:", 4},          {"y:", 8},        {"w:", 12},      {"h:", 16},       {"dvx:", 20},
    {"dvy:", 24},        {"fall:", 28},      {"arest:", 32},   {"vrest:", 36},  {"respond:", 40}, {"effect:", 44},
    {"pickingact:", 48}, {"pickedact:", 52}, {"bdefend:", 64}, {"injury:", 68}, {"zwidth:", 72},
};

static const Field BDY_FIELDS[] = {
    {"kind:", 0}, {"x:", 4}, {"y:", 8}, {"w:", 12}, {"h:", 16},
};

static uint32_t guest_allocate(uint32_t size)
{
    PUSH32(size);
    PUSH32(0x0040ef72);
    lf2_jit_call(0x004450ac);
    R(ESP) += 4;
    return R(EAX);
}

static int copy_token(ObjectTokenStream *stream, uint32_t destination, size_t capacity)
{
    char token[512];
    if (object_token_next(stream, token, sizeof token) != 1) return 0;
    const size_t length = strlen(token);
    if (length >= capacity) return 0;
    memcpy(g_mem + destination, token, length + 1);
    return 1;
}

static int write_int(ObjectTokenStream *stream, uint32_t destination)
{
    int32_t value;
    if (!object_token_int(stream, &value)) return 0;
    ST32(destination, (uint32_t)value);
    return 1;
}

static int mapped_int(ObjectTokenStream *stream, const char *key, uint32_t base, const Field *fields, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (strcmp(key, fields[i].name) == 0) return write_int(stream, base + fields[i].offset) ? 1 : -1;
    return 0;
}

static int parse_fixed_block(ObjectTokenStream *stream, uint32_t frame, const char *end, const Field *fields,
                             size_t count)
{
    char key[128];
    while (object_token_next(stream, key, sizeof key) == 1) {
        if (strcmp(key, end) == 0) return 1;
        if (mapped_int(stream, key, frame, fields, count) < 0) return 0;
    }
    return 0;
}

static int parse_itr(ObjectTokenStream *stream, uint32_t frame)
{
    const uint32_t count = LD32(frame + FRAME_ITR_COUNT) + 1;
    ST32(frame + FRAME_ITR_COUNT, count);
    if (count == 1) ST32(frame + FRAME_ITR_POINTER, guest_allocate(400));
    const uint32_t record = LD32(frame + FRAME_ITR_POINTER) + (count - 1) * 80;
    memset(g_mem + record, 0, 80);

    char key[128];
    while (object_token_next(stream, key, sizeof key) == 1) {
        if (strcmp(key, "itr_end:") == 0) return 1;
        if (strcmp(key, "catchingact:") == 0 || strcmp(key, "caughtact:") == 0) {
            const uint32_t offset = strcmp(key, "catchingact:") == 0 ? 48 : 56;
            if (!write_int(stream, record + offset) || !write_int(stream, record + offset + 4)) return 0;
            continue;
        }
        if (mapped_int(stream, key, record, ITR_FIELDS, sizeof ITR_FIELDS / sizeof ITR_FIELDS[0]) < 0) return 0;
    }
    return 0;
}

static int parse_bdy(ObjectTokenStream *stream, uint32_t frame)
{
    const uint32_t count = LD32(frame + FRAME_BDY_COUNT) + 1;
    ST32(frame + FRAME_BDY_COUNT, count);
    if (count == 1) ST32(frame + FRAME_BDY_POINTER, guest_allocate(200));
    const uint32_t record = LD32(frame + FRAME_BDY_POINTER) + (count - 1) * 40;
    memset(g_mem + record, 0, 40);
    return parse_fixed_block(stream, record, "bdy_end:", BDY_FIELDS, sizeof BDY_FIELDS / sizeof BDY_FIELDS[0]);
}

static void load_sound(const char *path, uint32_t frame)
{
    const size_t length = strlen(path);
    const uint32_t copy = guest_allocate((uint32_t)length + 1);
    memcpy(g_mem + copy, path, length + 1);
    ST32(frame + FRAME_SOUND_PATH, copy);

    uint32_t index = 0;
    const uint32_t count = LD32(SOUND_COUNT);
    while (index < count && strcmp((const char *)g_mem + SOUND_NAMES + index * 20, path) != 0) ++index;
    ST32(frame + FRAME_SOUND_INDEX, index);
    if (index < count) return;

    if (LD32(SOUND_ENABLED)) {
        PUSH32(copy);
        R(ECX) = SOUND_OBJECTS + index * 4;
        PUSH32(0x0040ef73);
        lf2_jit_call(0x004014e0);
        const uint32_t sound = LD32(SOUND_OBJECTS + index * 4);
        const uint32_t method = LD32(LD32(sound) + 60);
        PUSH32(0xffffd8f0u);
        PUSH32(sound);
        PUSH32(0x0040ef74);
        dispatch(method);
    }
    memcpy(g_mem + SOUND_NAMES + index * 20, path, length + 1);
    ST32(SOUND_COUNT, index + 1);
}

static int parse_sound(ObjectTokenStream *stream, uint32_t frame)
{
    char path[300];
    if (object_token_next(stream, path, sizeof path) != 1 || strlen(path) >= 20) return 0;
    load_sound(path, frame);
    return 1;
}

static void finish_bounds(uint32_t frame, uint32_t count_offset, uint32_t pointer_offset, uint32_t stride,
                          uint32_t bounds)
{
    const uint32_t count = LD32(frame + count_offset);
    if (!count) return;
    const uint32_t records = LD32(frame + pointer_offset);
    int32_t left = (int32_t)LD32(records + 4);
    int32_t top = (int32_t)LD32(records + 8);
    int32_t right = left + (int32_t)LD32(records + 12);
    int32_t bottom = top + (int32_t)LD32(records + 16);
    for (uint32_t i = 1; i < count; ++i) {
        const uint32_t record = records + i * stride;
        const int32_t x = (int32_t)LD32(record + 4);
        const int32_t y = (int32_t)LD32(record + 8);
        const int32_t x2 = x + (int32_t)LD32(record + 12);
        const int32_t y2 = y + (int32_t)LD32(record + 16);
        if (x < left) left = x;
        if (y < top) top = y;
        if (x2 > right) right = x2;
        if (y2 > bottom) bottom = y2;
    }
    ST32(frame + bounds, (uint32_t)left);
    ST32(frame + bounds + 4, (uint32_t)top);
    ST32(frame + bounds + 8, (uint32_t)(right - left));
    ST32(frame + bounds + 12, (uint32_t)(bottom - top));
}

static int parse_frame(ObjectTokenStream *stream, uint32_t object)
{
    int32_t id;
    if (!object_token_int(stream, &id) || id < 0 || id >= FRAME_COUNT) return 0;
    const uint32_t frame = object + (uint32_t)id * FRAME_STRIDE;
    if (!copy_token(stream, frame + FRAME_NAME, 20)) return 0;
    ST32(frame + FRAME_ITR_COUNT, 0);
    ST32(frame + FRAME_BDY_COUNT, 0);
    ST8(frame + FRAME_ACTIVE, 1);

    char key[128];
    while (object_token_next(stream, key, sizeof key) == 1) {
        if (strcmp(key, "<frame_end>") == 0) {
            finish_bounds(frame, FRAME_ITR_COUNT, FRAME_ITR_POINTER, 80, 0x8dc);
            finish_bounds(frame, FRAME_BDY_COUNT, FRAME_BDY_POINTER, 40, 0x8ec);
            return 1;
        }
        const int mapped = mapped_int(stream, key, object + (uint32_t)id * FRAME_STRIDE, FRAME_FIELDS,
                                      sizeof FRAME_FIELDS / sizeof FRAME_FIELDS[0]);
        if (mapped < 0) return 0;
        if (mapped > 0) continue;
        if (strcmp(key, "sound:") == 0) {
            if (!parse_sound(stream, frame)) return 0;
        } else if (strcmp(key, "opoint:") == 0) {
            if (!parse_fixed_block(stream, frame, "opoint_end:", OPOINT_FIELDS,
                                   sizeof OPOINT_FIELDS / sizeof OPOINT_FIELDS[0]))
                return 0;
        } else if (strcmp(key, "bpoint:") == 0) {
            if (!parse_fixed_block(stream, frame, "bpoint_end:", BPOINT_FIELDS,
                                   sizeof BPOINT_FIELDS / sizeof BPOINT_FIELDS[0]))
                return 0;
        } else if (strcmp(key, "cpoint:") == 0) {
            if (!parse_fixed_block(stream, frame, "cpoint_end:", CPOINT_FIELDS,
                                   sizeof CPOINT_FIELDS / sizeof CPOINT_FIELDS[0]))
                return 0;
        } else if (strcmp(key, "wpoint:") == 0) {
            if (!parse_fixed_block(stream, frame, "wpoint_end:", WPOINT_FIELDS,
                                   sizeof WPOINT_FIELDS / sizeof WPOINT_FIELDS[0]))
                return 0;
        } else if (strcmp(key, "itr:") == 0) {
            if (!parse_itr(stream, frame)) return 0;
        } else if (strcmp(key, "bdy:") == 0) {
            if (!parse_bdy(stream, frame)) return 0;
        }
    }
    return 0;
}

int object_parser_load_frames(uint32_t object, const char *text, size_t size)
{
    ObjectTokenStream stream = {text, text + size};
    char token[128];
    while (object_token_next(&stream, token, sizeof token) == 1) {
        object_parser_checksum_token(token);
        if (strcmp(token, "<frame>") == 0 && !parse_frame(&stream, object)) return 0;
    }
    return 1;
}
