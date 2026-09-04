#include "startup_init.h"

#include "guest.h"
#include "jit_executor.h"

#include <stddef.h>
#include <stdint.h>

enum {
    WORLD_INIT_LATCH = 0x0044d05c,
    OBJECT_COUNT = 400,
    OBJECT_BYTES = 0x420,
    REGISTRY_BYTES = 0x04d823a8,
    GRAPHIC_BYTES = 0x1f50,
};

typedef struct {
    uint32_t sound_slot;
    uint32_t path;
} SoundSpec;

typedef struct {
    uint32_t slot;
    uint32_t name;
} GraphicSpec;

typedef struct {
    uint32_t x_value;
    uint32_t y_value;
} PlayerSeed;

static const SoundSpec STARTUP_SOUNDS[] = {
    {0x00451db0, 0x00449588}, {0x00451db4, 0x00449578}, {0x00451db8, 0x00449568}, {0x00451dbc, 0x00449558},
    {0x00451dc0, 0x00449548}, {0x00451dc4, 0x00449538}, {0x00451dc8, 0x00449528}, {0x00451dcc, 0x00449518},
    {0x00451dd0, 0x00449508}, {0x00451dd4, 0x004494f8}, {0x00451dd8, 0x004494e8}, {0x00451ddc, 0x004494d8},
    {0x00451de0, 0x004494c8}, {0x00451de4, 0x004494b8}, {0x00451de8, 0x004494a8}, {0x00451dec, 0x00449498},
    {0x00451df0, 0x00449488}, {0x00451df4, 0x00449478},
};

static const GraphicSpec GAMEPLAY_GRAPHICS[] = {
    {0x0044ff8c, 0x00449454}, /* PAUSE */
    {0x0044f8f8, 0x0044944c}, /* DEMO */
    {0x0044fcb4, 0x0044943c}, /* SCORE_BOARD1 */
    {0x0044fd8c, 0x0044942c}, /* SCORE_BOARD2 */
    {0x0044f88c, 0x0044941c}, /* SCORE_BOARD3 */
    {0x0044f87c, 0x0044940c}, /* SCORE_BOARD4 */
    {0x0044fd90, 0x00449400}, /* WIN_ALIVE */
    {0x0044fd94, 0x004493f4}, /* WIN_DEAD */
    {0x0044fb64, 0x004493e8}, /* LOSE_DEAD */
    {0x0044fd7c, 0x004493e0}, /* BARS */
};

static const PlayerSeed PLAYER_SEEDS[] = {
    {0x00449470, 0x00447928}, /* 200, 0 */
    {0x00449468, 0x00447928}, /* 210, 0 */
    {0x00449468, 0x00447928}, /* 210, 0 */
    {0x00449468, 0x00447928}, /* 210, 0 */
    {0x00447938, 0x00447930}, /* 580, -200 */
    {0x00449460, 0x00447928}, /* 570, 0 */
    {0x00447938, 0x00447930}, /* 580, -200 */
    {0x00449460, 0x00447928}, /* 570, 0 */
};

static uint32_t guest_allocate(uint32_t bytes, uint32_t return_address)
{
    PUSH32(bytes);
    PUSH32(return_address);
    lf2_jit_call(0x004450ac);
    R(ESP) += 4;
    return R(EAX);
}

static void object_reset(uint32_t object)
{
    R(ECX) = object;
    PUSH32(0x0041c096);
    lf2_jit_call(0x004061d0);
}

static uint32_t graphic_create(uint32_t name)
{
    const uint32_t graphic = guest_allocate(GRAPHIC_BYTES, 0x0041c2ff);
    if (!graphic) return 0;
    PUSH32(0);
    PUSH32(name);
    PUSH32(0x40);
    R(ECX) = graphic;
    PUSH32(0x0041c325);
    lf2_jit_call(0x0043ee50);
    return R(EAX);
}

static void clear_runtime_tables(void)
{
    for (uint32_t i = 0; i < 400; ++i) ST32(0x00457588 + 4 * i, 0);
    for (uint32_t i = 0; i < 80; ++i) ST32(0x00453e10 + 4 * i, 0);
}

static void load_sound_effects(void)
{
    for (size_t i = 0; i < sizeof STARTUP_SOUNDS / sizeof STARTUP_SOUNDS[0]; ++i) {
        PUSH32(STARTUP_SOUNDS[i].path);
        R(ECX) = STARTUP_SOUNDS[i].sound_slot;
        PUSH32(0x0041bed7 + (uint32_t)(15 * i));
        lf2_jit_call(0x004014e0);
    }
    ST32(0x0045843c, (uint32_t)(sizeof STARTUP_SOUNDS / sizeof STARTUP_SOUNDS[0]));
}

static uint32_t create_registry(uint32_t frame_surface)
{
    const uint32_t allocation = guest_allocate(REGISTRY_BYTES, 0x0041bff5);
    if (!allocation) return 0;
    PUSH32(frame_surface);
    PUSH32(0x00448980); /* data\\data.txt */
    R(ECX) = allocation;
    PUSH32(0x0041c018);
    lf2_jit_call(0x004122f0);
    return R(EAX);
}

static void initialise_object(uint32_t object, uint32_t data)
{
    object_reset(object);
    guest_store_f64(object + 88, guest_load_f64(0x00449470)); /* 200 */
    guest_store_f64(object + 96, 0.0);
    guest_store_f64(object + 104, guest_load_f64(0x00447928)); /* 300 */
    ST32(object + 872, data);
}

static void create_object_pool(uint32_t world, uint32_t data)
{
    for (uint32_t i = 0; i < OBJECT_COUNT; ++i) {
        const uint32_t object = guest_allocate(OBJECT_BYTES, 0x0041c07a);
        if (object) initialise_object(object, data);
        ST32(world + 404 + 4 * i, object);
        ST8(world + 4 + i, 0);
    }
}

static void seed_player_slots(uint32_t world, uint32_t data)
{
    for (size_t i = 0; i < sizeof PLAYER_SEEDS / sizeof PLAYER_SEEDS[0]; ++i) {
        const uint32_t object = LD32(world + 404 + 4 * i);
        object_reset(object);
        ST32(object + 872, data);
        ST32(object + 796, LD32(data + 144));
        guest_store_f64(object + 88, guest_load_f64(PLAYER_SEEDS[i].x_value));
        guest_store_f64(object + 96, guest_load_f64(PLAYER_SEEDS[i].y_value));
        guest_store_f64(object + 104, guest_load_f64(0x00447928)); /* 300 */
        ST8(world + 4 + i, 1);
    }
}

static void create_gameplay_graphics(void)
{
    for (size_t i = 0; i < sizeof GAMEPLAY_GRAPHICS / sizeof GAMEPLAY_GRAPHICS[0]; ++i)
        ST32(GAMEPLAY_GRAPHICS[i].slot, graphic_create(GAMEPLAY_GRAPHICS[i].name));
}

void startup_world_initialise(uint32_t world, uint32_t frame_surface)
{
    clear_runtime_tables();

    startup_init_step_begin(STARTUP_INIT_SOUND_EFFECTS, "sound-effects");
    load_sound_effects();
    startup_init_step_done(STARTUP_INIT_SOUND_EFFECTS, "sound-effects");

    startup_init_step_begin(STARTUP_INIT_OBJECT_REGISTRY, "object-registry");
    const uint32_t registry = create_registry(frame_surface);
    ST32(world + 2004, registry);
    const uint32_t data = LD32(registry);
    startup_init_step_done(STARTUP_INIT_OBJECT_REGISTRY, "object-registry");

    startup_init_step_begin(STARTUP_INIT_OBJECT_POOL, "object-pool");
    create_object_pool(world, data);
    startup_init_step_done(STARTUP_INIT_OBJECT_POOL, "object-pool");

    startup_init_step_begin(STARTUP_INIT_PLAYER_SLOTS, "player-slots");
    seed_player_slots(world, data);
    startup_init_step_done(STARTUP_INIT_PLAYER_SLOTS, "player-slots");

    startup_init_step_begin(STARTUP_INIT_GAMEPLAY_RESOURCES, "gameplay-resources");
    create_gameplay_graphics();
    ST32(WORLD_INIT_LATCH, 0);
    startup_init_step_done(STARTUP_INIT_GAMEPLAY_RESOURCES, "gameplay-resources");
}
