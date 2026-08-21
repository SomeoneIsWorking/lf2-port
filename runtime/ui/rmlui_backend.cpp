#include "rmlui_backend.h"
#include "device_assets.h"

RmlUiRenderBackend::RmlUiRenderBackend(SDL_Renderer *renderer) : renderer(renderer)
{
    blend =
        SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                   SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

void RmlUiRenderBackend::BeginFrame()
{
    /* RmlUi's clip rectangles belong to one document render. The game renderer uses the same
     * SDL_Renderer before and after us, so inheriting the last element's clip on the next frame
     * clips the game itself to that element. Start and finish at the shared boundary's neutral
     * state, just as RmlUi's reference SDL backend resets its viewport before drawing. */
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderClipRect(renderer, nullptr);
    scissor_enabled = false;
    SDL_SetRenderDrawBlendMode(renderer, blend);
}

void RmlUiRenderBackend::EndFrame()
{
    SDL_SetRenderClipRect(renderer, nullptr);
    SDL_SetRenderViewport(renderer, nullptr);
    scissor_enabled = false;
}

Rml::CompiledGeometryHandle RmlUiRenderBackend::CompileGeometry(Rml::Span<const Rml::Vertex> source_vertices,
                                                                Rml::Span<const int> source_indices)
{
    auto geometry = std::make_unique<Geometry>();
    geometry->vertex_count = (int)source_vertices.size();
    geometry->vertices.reset(new SDL_Vertex[geometry->vertex_count]);
    for (int i = 0; i < geometry->vertex_count; i++) {
        const Rml::Vertex &v = source_vertices[i];
        geometry->vertices[i].position = {v.position.x, v.position.y};
        geometry->vertices[i].tex_coord = {v.tex_coord.x, v.tex_coord.y};
        geometry->vertices[i].color = {
            static_cast<float>(v.colour.red) / 255.f, static_cast<float>(v.colour.green) / 255.f,
            static_cast<float>(v.colour.blue) / 255.f, static_cast<float>(v.colour.alpha) / 255.f};
    }
    geometry->index_count = (int)source_indices.size();
    geometry->indices.reset(new int[geometry->index_count]);
    std::memcpy(geometry->indices.get(), source_indices.data(), (size_t)geometry->index_count * sizeof(int));
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
}

void RmlUiRenderBackend::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{ delete reinterpret_cast<Geometry *>(handle); }

void RmlUiRenderBackend::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                        Rml::TextureHandle texture)
{
    Geometry *geometry = reinterpret_cast<Geometry *>(handle);
    std::unique_ptr<SDL_Vertex[]> vertices(new SDL_Vertex[geometry->vertex_count]);
    for (int i = 0; i < geometry->vertex_count; i++) {
        vertices[i] = geometry->vertices[i];
        vertices[i].position.x += translation.x;
        vertices[i].position.y += translation.y;
    }
    SDL_RenderGeometry(renderer, reinterpret_cast<SDL_Texture *>(texture), vertices.get(), geometry->vertex_count,
                       geometry->indices.get(), geometry->index_count);
}

Rml::TextureHandle RmlUiRenderBackend::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source)
{
    const DeviceAsset asset = device_asset_from_source(source.c_str());
    if (asset == DEVICE_ASSET_INVALID) return 0;
    /* RmlUi draws directly in output pixels. Keep a 4x raster behind the 30dp column icon so
     * resizing or a high-density output never magnifies the SVG's nominal 72px canvas. */
    SDL_Surface *surface = device_asset_rasterize(asset, 120, 120);
    if (!surface) return 0;
    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        surface = converted;
    }
    if (!surface || !SDL_PremultiplySurfaceAlpha(surface, false)) {
        if (surface) SDL_DestroySurface(surface);
        return 0;
    }

    dimensions = {surface->w, surface->h};
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, blend);
        /* The shared SVGs are deliberately rasterized above their CSS size. Linear sampling
         * preserves their antialiased vector edges when RmlUi reduces that texture; nearest
         * sampling made the 120px source look like a low-resolution sprite at 30dp. */
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        shared_device_textures_loaded++;
    }
    return reinterpret_cast<Rml::TextureHandle>(texture);
}

Rml::TextureHandle RmlUiRenderBackend::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, dimensions.x, dimensions.y);
    if (!texture) return 0;
    SDL_SetTextureBlendMode(texture, blend);
    /* SDL3 returns true on success. Treating it like SDL2's integer status destroyed every
     * successfully uploaded font atlas, leaving RmlUi to draw glyph quads as solid blocks. */
    if (!SDL_UpdateTexture(texture, nullptr, source.data(), dimensions.x * 4)) {
        SDL_DestroyTexture(texture);
        return 0;
    }
    /* FreeType rasterizes glyphs at the computed dp pixel size. Linear sampling preserves
     * that coverage for fractional placement without turning the atlas into a fixed-size UI
     * buffer; the geometry and atlas both remain in drawable pixels. */
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return reinterpret_cast<Rml::TextureHandle>(texture);
}

void RmlUiRenderBackend::ReleaseTexture(Rml::TextureHandle handle)
{
    if (handle) SDL_DestroyTexture(reinterpret_cast<SDL_Texture *>(handle));
}

void RmlUiRenderBackend::EnableScissorRegion(bool enable)
{
    SDL_SetRenderClipRect(renderer, enable ? &scissor : nullptr);
    scissor_enabled = enable;
}

void RmlUiRenderBackend::SetScissorRegion(Rml::Rectanglei region)
{
    scissor.x = region.Left();
    scissor.y = region.Top();
    scissor.w = region.Width();
    scissor.h = region.Height();
    if (scissor_enabled) SDL_SetRenderClipRect(renderer, &scissor);
}
