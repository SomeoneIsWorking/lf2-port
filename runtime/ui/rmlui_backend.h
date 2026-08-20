/* SDL_Render implementation of RmlUi's rendering boundary.
 *
 * This is intentionally ignorant of settings, bindings and document structure. Dusklight's
 * backend has the same ownership boundary but targets Aurora/WebGPU; LF2 keeps its SDL backend
 * and copies the separation, not an incompatible renderer implementation.
 */
#ifndef LF2_RMLUI_BACKEND_H
#define LF2_RMLUI_BACKEND_H

#include <RmlUi/Core/RenderInterface.h>
#include <SDL3/SDL.h>

#include <memory>

class RmlUiRenderBackend : public Rml::RenderInterface {
public:
    explicit RmlUiRenderBackend(SDL_Renderer *renderer);
    void BeginFrame();
    void EndFrame();
    int SharedDeviceTexturesLoaded() const { return shared_device_textures_loaded; }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i &, const Rml::String &) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle handle) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

private:
    struct Geometry {
        std::unique_ptr<SDL_Vertex[]> vertices;
        int vertex_count = 0;
        std::unique_ptr<int[]> indices;
        int index_count = 0;
    };

    SDL_Renderer *renderer;
    SDL_BlendMode blend;
    SDL_Rect scissor{};
    bool scissor_enabled = false;
    int shared_device_textures_loaded = 0;
};

#endif
