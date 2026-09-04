/* The SDL_GPU engine's one texture cache.
 *
 * A key names either a mutable guest surface or a host-owned premultiplied font/SVG tile.
 * Guest XRGB is converted to portable RGBA8 here and colour keys become alpha; host ARGB
 * keeps its coverage alpha. Explicit dirty notifications are authoritative and the sampled
 * hash catches writers outside DirectDraw's known paths.
 *
 * Capacity bounds simultaneous residency, not the lifetime number of sheets. Stage mode
 * exceeds 512 distinct lifetime keys, so a full cache reuses the least-recently-used entry
 * that the current frame has not referenced. A replacement is uploaded before the old entry
 * is released, leaving the last valid cache state intact if allocation or upload fails. */
#include "lf2_log.h"
#include "engine_textures.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdio.h>

#include "guest.h"
#include "texture_lru.h"

enum { TEXTURE_CAPACITY = 512 };

typedef struct {
    uint32_t pixels;
    const void *host;
    int keyed;
    uint32_t key_lo, key_hi;
    int w, h;
    uint32_t content;
    int dirty;
    SDL_GPUTexture *texture;
} EngineTexture;

static SDL_GPUDevice *device;
static EngineTexture textures[TEXTURE_CAPACITY];
static TextureLruEntry usage[TEXTURE_CAPACITY];
static int texture_count;
static uint64_t frame_id;
static int frame_live;
static int peak_frame_live;
static long upload_count;
static long eviction_count;
static long failure_count;

static uint32_t sample_hash(const uint8_t *base, int w, int h, int pitch)
{
    uint32_t hash = 2166136261u;
    for (int y = 0; y < h; y += 7) {
        const uint32_t *row = (const uint32_t *)(base + (size_t)y * (size_t)pitch);
        for (int x = 0; x < w; x += 5) {
            hash ^= row[x];
            hash *= 16777619u;
        }
    }
    hash ^= (uint32_t)w * 2654435761u;
    hash ^= (uint32_t)h * 40503u;
    return hash;
}

static void fill_rgba(uint8_t *dst, const EngineQuad *quad, int w, int h)
{
    if (quad->host_argb) {
        for (int y = 0; y < h; y++) {
            const uint32_t *src =
                (const uint32_t *)((const uint8_t *)quad->host_argb + (size_t)y * (size_t)quad->host_pitch);
            uint8_t *row = dst + (size_t)y * (size_t)w * 4;
            for (int x = 0; x < w; x++) {
                const uint32_t value = src[x];
                row[x * 4 + 0] = (uint8_t)((value >> 16) & 0xff);
                row[x * 4 + 1] = (uint8_t)((value >> 8) & 0xff);
                row[x * 4 + 2] = (uint8_t)(value & 0xff);
                row[x * 4 + 3] = (uint8_t)((value >> 24) & 0xff);
            }
        }
        return;
    }

    const uint8_t *base = g_mem + quad->src_pixels;
    const uint32_t lo = quad->key_lo & 0x00ffffffu;
    const uint32_t hi = quad->key_hi & 0x00ffffffu;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)(base + (size_t)y * (size_t)quad->spitch);
        uint8_t *row = dst + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            const uint32_t value = src[x] & 0x00ffffffu;
            const int clear = quad->keyed && value >= lo && value <= hi;
            row[x * 4 + 0] = (uint8_t)(clear ? 0 : (value >> 16) & 0xff);
            row[x * 4 + 1] = (uint8_t)(clear ? 0 : (value >> 8) & 0xff);
            row[x * 4 + 2] = (uint8_t)(clear ? 0 : value & 0xff);
            row[x * 4 + 3] = (uint8_t)(clear ? 0 : 255);
        }
    }
}

static int upload(EngineTexture *entry, const EngineQuad *quad)
{
    const int w = entry->w;
    const int h = entry->h;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    void *mapped = transfer ? SDL_MapGPUTransferBuffer(device, transfer, false) : NULL;
    if (!mapped) {
        lf2_log_writef(LF2_LOG_INFO, "engine_textures", "engine textures: a %dx%d upload could not be mapped: %s\n", w,
                       h, SDL_GetError());
        if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
        return 0;
    }
    fill_rgba((uint8_t *)mapped, quad, w, h);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(device);
    if (!commands) {
        lf2_log_writef(LF2_LOG_INFO, "engine_textures", "engine textures: no upload command buffer: %s\n",
                       SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return 0;
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
    if (!copy) {
        lf2_log_writef(LF2_LOG_INFO, "engine_textures", "engine textures: no upload copy pass: %s\n", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commands);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return 0;
    }
    SDL_GPUTextureTransferInfo source = {transfer, 0, (Uint32)w, (Uint32)h};
    SDL_GPUTextureRegion destination;
    SDL_zero(destination);
    destination.texture = entry->texture;
    destination.w = (Uint32)w;
    destination.h = (Uint32)h;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    const int submitted = SDL_SubmitGPUCommandBuffer(commands);
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    if (!submitted) {
        lf2_log_writef(LF2_LOG_INFO, "engine_textures", "engine textures: a %dx%d upload could not be submitted: %s\n",
                       w, h, SDL_GetError());
        return 0;
    }
    upload_count++;
    return 1;
}

static int same_key(const EngineTexture *entry, const EngineQuad *quad, int w, int h)
{
    if (entry->w != w || entry->h != h || entry->keyed != quad->keyed) return 0;
    if (quad->host_argb ? entry->host != quad->host_argb : entry->pixels != quad->src_pixels) return 0;
    return !quad->keyed || (entry->key_lo == quad->key_lo && entry->key_hi == quad->key_hi);
}

static void touch(int index)
{
    if (usage[index].last_frame != frame_id) {
        usage[index].last_frame = frame_id;
        frame_live++;
        if (frame_live > peak_frame_live) peak_frame_live = frame_live;
    }
}

static SDL_GPUTexture *make_texture(int w, int h)
{
    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = (Uint32)w;
    info.height = (Uint32)h;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return SDL_CreateGPUTexture(device, &info);
}

void engine_textures_init(SDL_GPUDevice *gpu_device)
{
    device = gpu_device;
}

void engine_textures_begin_frame(void)
{
    frame_id++;
    if (!frame_id) {
        for (int i = 0; i < texture_count; i++) usage[i].last_frame = 0;
        frame_id = 1;
    }
    frame_live = 0;
}

SDL_GPUTexture *engine_texture_for(const EngineQuad *quad)
{
    const int w = quad->host_argb ? quad->host_w : quad->sw;
    const int h = quad->host_argb ? quad->host_h : quad->sh;
    if (w <= 0 || h <= 0) return NULL;

    const uint32_t content = quad->host_argb ? sample_hash((const uint8_t *)quad->host_argb, w, h, quad->host_pitch)
                                             : sample_hash(g_mem + quad->src_pixels, w, h, quad->spitch);
    for (int i = 0; i < texture_count; i++) {
        EngineTexture *entry = &textures[i];
        if (!same_key(entry, quad, w, h)) continue;
        if (entry->dirty || entry->content != content) {
            if (!upload(entry, quad)) {
                failure_count++;
                return NULL;
            }
            entry->content = content;
            entry->dirty = 0;
        }
        touch(i);
        return entry->texture;
    }

    int slot = texture_count;
    if (texture_count == TEXTURE_CAPACITY) {
        slot = texture_lru_choose(usage, texture_count, frame_id);
        if (slot < 0) {
            failure_count++;
            lf2_log_writef(LF2_LOG_INFO, "engine_textures",
                           "engine textures: all %d entries are live in one frame; art is "
                           "missing from this frame\n",
                           TEXTURE_CAPACITY);
            return NULL;
        }
    }

    EngineTexture replacement = {
        .pixels = quad->host_argb ? 0u : quad->src_pixels,
        .host = quad->host_argb,
        .keyed = quad->keyed,
        .key_lo = quad->key_lo,
        .key_hi = quad->key_hi,
        .w = w,
        .h = h,
        .content = content,
        .dirty = 0,
        .texture = make_texture(w, h),
    };
    if (!replacement.texture) {
        failure_count++;
        lf2_log_writef(LF2_LOG_INFO, "engine_textures", "engine textures: could not create a %dx%d texture: %s\n", w, h,
                       SDL_GetError());
        return NULL;
    }
    if (!upload(&replacement, quad)) {
        failure_count++;
        SDL_ReleaseGPUTexture(device, replacement.texture);
        return NULL;
    }

    if (slot == texture_count) {
        texture_count++;
    } else {
        SDL_ReleaseGPUTexture(device, textures[slot].texture);
        eviction_count++;
    }
    textures[slot] = replacement;
    usage[slot].last_frame = 0;
    touch(slot);
    return replacement.texture;
}

void engine_textures_surface_dirty(uint32_t pixels)
{
    for (int i = 0; i < texture_count; i++) {
        if (textures[i].pixels == pixels) textures[i].dirty = 1;
    }
}

void engine_textures_report(void)
{
    lf2_log_writef(LF2_LOG_INFO, "engine_textures",
                   "engine textures: %d resident, %ld upload(s), %ld eviction(s), %d peak live/frame, "
                   "%ld request(s) failed\n",
                   texture_count, upload_count, eviction_count, peak_frame_live, failure_count);
}

void engine_textures_shutdown(void)
{
    if (!device) return;
    for (int i = 0; i < texture_count; i++) {
        if (textures[i].texture) SDL_ReleaseGPUTexture(device, textures[i].texture);
        textures[i] = (EngineTexture){0};
        usage[i] = (TextureLruEntry){0};
    }
    texture_count = 0;
    device = NULL;
}
