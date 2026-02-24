//========== Copyright (c) 2015, PG & 2025, WH, All rights reserved. ============//
//
// Purpose:		resource manager
//
// $NoKeywords: $rm
//===============================================================================//

#pragma once
#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

#include "config.h"

#include "noinclude.h"
#include "types.h"
#include "StaticPImpl.h"

#include <string_view>
#include <vector>
#include <memory>
#include <string>
#include <span>

enum class MultisampleType : uint8_t;
enum class DrawPrimitive : uint8_t;
enum class DrawUsageType : uint8_t;

class Image;
class McFont;
class Resource;
class Sound;
class RenderTarget;
class Shader;
class VertexArrayObject;
class TextureAtlas;
class AsyncResourceLoader;
struct ResourceManagerImpl;

// clang-format off
enum class ResourceDestroyFlags : uint8_t {
    RDF_DEFAULT         = 0u,
    RDF_FORCE_BLOCKING  = 1u << 0,
    RDF_FORCE_ASYNC     = 1u << 1, // TODO (nontrivial to implement properly)
    RDF_NODELETE        = 1u << 2, // implies FORCE_BLOCKING
};
// clang-format on

MAKE_FLAG_ENUM(ResourceDestroyFlags)

class ResourceManager final {
    NOCOPY_NOMOVE(ResourceManager)
   public:
    ResourceManager();
    ~ResourceManager();

    void loadResource(Resource *rs) {
        requestNextLoadUnmanaged();
        loadResource(rs, true);
    }

    void destroyResource(Resource *rs, ResourceDestroyFlags flags = ResourceDestroyFlags::RDF_DEFAULT);

    // async reload
    void reloadResource(Resource *rs, bool async = false);
    void reloadResources(const std::vector<Resource *> &resources, bool async = false);

    void requestNextLoadAsync();
    void requestNextLoadUnmanaged();

    [[nodiscard]] size_t getSyncLoadMaxBatchSize() const;
    void setSyncLoadMaxBatchSize(size_t resourcesToLoad);

    // resources which will be garbage collected on shutdown
    // userPtr must contain a pre-created (allocated with new) resource of any type
    // returns true if it was successfully added to tracking
    bool addManagedResource(Resource *userPtr, const std::string &resourceName);

    // images
    Image *loadImage(std::string filepath, const std::string &resourceName, bool mipmapped = false,
                     bool keepInSystemMemory = false);
    Image *loadImageUnnamed(std::string filepath, bool mipmapped = false, bool keepInSystemMemory = false);
    Image *loadImageAbs(std::string absoluteFilepath, const std::string &resourceName, bool mipmapped = false,
                        bool keepInSystemMemory = false);
    Image *loadImageAbsUnnamed(std::string absoluteFilepath, bool mipmapped = false, bool keepInSystemMemory = false);
    Image *createImage(i32 width, i32 height, bool mipmapped = false, bool keepInSystemMemory = false);

    // fonts
    McFont *loadFont(std::string filepath, const std::string &resourceName, int fontSize = 16, bool antialiasing = true,
                     int fontDPI = 96);
    McFont *loadFont(std::string filepath, const std::string &resourceName, const std::span<const char16_t> &characters,
                     int fontSize = 16, bool antialiasing = true, int fontDPI = 96);

    // sounds
    Sound *loadSound(std::string filepath, const std::string &resourceName, bool stream = false,
                     bool overlayable = false, bool loop = false);
    Sound *loadSoundAbs(std::string filepath, const std::string &resourceName, bool stream = false,
                        bool overlayable = false, bool loop = false);

    // shaders
    Shader *loadShader(std::string vertexShaderFilePath, std::string fragmentShaderFilePath,
                       const std::string &resourceName);
    Shader *loadShader(std::string vertexShaderFilePath, std::string fragmentShaderFilePath);
    Shader *createShader(std::string vertexShader, std::string fragmentShader, const std::string &resourceName);
    Shader *createShader(std::string vertexShader, std::string fragmentShader);

    // automatically loads opengl/dx11 vertex and fragment shaders
    // sets the resource name to the shaderBasename as well
    Shader *createShaderAuto(std::string_view shaderBasename);

    // rendertargets
    RenderTarget *createRenderTarget(int x, int y, int width, int height,
                                     MultisampleType multiSampleType = MultisampleType{0});
    RenderTarget *createRenderTarget(int width, int height, MultisampleType multiSampleType = MultisampleType{0});

    // texture atlas
    TextureAtlas *createTextureAtlas(int width, int height, bool filtering = false);

    // models/meshes
    VertexArrayObject *createVertexArrayObject(DrawPrimitive primitive = DrawPrimitive{2},
                                               DrawUsageType usage = DrawUsageType{0}, bool keepInSystemMemory = false);

    // resource access by name
    [[nodiscard]] Image *getImage(std::string_view resourceName) const;
    [[nodiscard]] McFont *getFont(std::string_view resourceName) const;
    [[nodiscard]] Sound *getSound(std::string_view resourceName) const;
    [[nodiscard]] Shader *getShader(std::string_view resourceName) const;

    // methods for getting all resources of a type
    [[nodiscard]] const std::vector<Image *> &getImages() const;
    [[nodiscard]] const std::vector<McFont *> &getFonts() const;
    [[nodiscard]] const std::vector<Sound *> &getSounds() const;
    [[nodiscard]] const std::vector<Shader *> &getShaders() const;
    [[nodiscard]] const std::vector<RenderTarget *> &getRenderTargets() const;
    [[nodiscard]] const std::vector<TextureAtlas *> &getTextureAtlases() const;
    [[nodiscard]] const std::vector<VertexArrayObject *> &getVertexArrayObjects() const;
    [[nodiscard]] const std::vector<Resource *> &getResources() const;

    [[nodiscard]] bool isLoading() const;
    [[nodiscard]] bool isLoadingResource(const Resource *rs) const;
    bool waitForResource(Resource *rs);
    [[nodiscard]] size_t getNumInFlight() const;
    [[nodiscard]] size_t getNumAsyncDestroyQueue() const;

   private:
    void destroyResources();
    void loadResource(Resource *res, bool load);

    friend class Engine;
    void update();

    friend struct ResourceManagerImpl;
    StaticPImpl<ResourceManagerImpl, 1536> pImpl;  // implementation details
};

// define/managed in Engine.cpp, declared here for convenience
extern std::unique_ptr<ResourceManager> resourceManager;

#endif
