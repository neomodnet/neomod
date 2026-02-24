//========== Copyright (c) 2015, PG & 2025, WH, All rights reserved. ============//
//
// Purpose:		resource manager
//
// $NoKeywords: $rm
//===============================================================================//

#include "ResourceManager.h"

#include "Font.h"
#include "Image.h"
#include "RenderTarget.h"
#include "Shader.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "TextureAtlas.h"
#include "Timing.h"
#include "VertexArrayObject.h"
#include "Hashing.h"

#include "SyncMutex.h"

#include "App.h"
#include "AsyncResourceLoader.h"
#include "ConVar.h"
#include "Resource.h"
#include "Engine.h"
#include "Logging.h"
#include "Environment.h"

#include "binary_embed.h"

#include <stack>
#include <utility>

static_assert(MultisampleType{0} == MultisampleType::X0);
static_assert(DrawPrimitive{2} == DrawPrimitive::TRIANGLES);
static_assert(DrawUsageType{0} == DrawUsageType::STATIC);

struct ResourceManagerImpl final {
    NOCOPY_NOMOVE(ResourceManagerImpl)
   public:
    ResourceManagerImpl() : asyncLoader() /* create async loader instance */ {
        // create directories we will assume already exist later on
        Environment::createDirectory(MCENGINE_FONTS_PATH);
        Environment::createDirectory(MCENGINE_IMAGES_PATH);
        Environment::createDirectory(MCENGINE_SHADERS_PATH);
        Environment::createDirectory(MCENGINE_SOUNDS_PATH);

        this->bNextLoadAsync.store(false, std::memory_order_release);

        // reserve space for typed vectors
        this->vImages.reserve(256);
        this->vFonts.reserve(16);
        this->vSounds.reserve(64);
        this->vShaders.reserve(32);
        this->vRenderTargets.reserve(8);
        this->vTextureAtlases.reserve(8);
        this->vVertexArrayObjects.reserve(32);
    }
    ~ResourceManagerImpl() = default;

    template <typename T>
    [[nodiscard]] T *tryGet(std::string_view resourceName) const {
        if(resourceName.empty()) return nullptr;
        if(auto it = this->mNameToResourceMap.find(resourceName); it != this->mNameToResourceMap.end()) {
            return it->second->as<T>();
        }
        logIfCV(debug_rm, R"(W: Resource with name "{:s}" does not exist!)", resourceName);
        return nullptr;
    }

    template <typename T>
    [[nodiscard]] T *checkIfExistsAndHandle(std::string_view resourceName) {
        if(resourceName.empty()) return nullptr;
        auto it = this->mNameToResourceMap.find(resourceName);
        if(it == this->mNameToResourceMap.end()) {
            return nullptr;
        }
        logIfCV(debug_rm, R"(Resource with name "{:s}" already loaded.)", resourceName);
        // handle flags (reset them)
        resetFlags();
        return it->second->as<T>();
    }

    void resetFlags() {
        {
            Sync::unique_lock lock(this->managedLoadMutex);
            if(this->nextLoadUnmanagedStack.size() > 0) this->nextLoadUnmanagedStack.pop_back();
        }

        this->bNextLoadAsync.store(false, std::memory_order_release);
    }

    // add a managed resource to the main resources vector + the name map and typed vectors
    void addManagedResource(Resource *res) {
        if(!res) return;
        logIfCV(debug_rm, "Adding managed {}", res->getDebugIdentifier());

        this->vResources.push_back(res);

        if(res->getName().length() > 0) this->mNameToResourceMap.try_emplace(res->getName(), res);
        addResourceToTypedVector(res);
    }

    // remove a managed resource from the main resources vector + the name map and typed vectors
    void removeManagedResource(Resource *res, std::vector<Resource *>::iterator resIterator) {
        if(!res) return;
        logIfCV(debug_rm, "Removing managed {}", res->getDebugIdentifier());

        this->vResources.erase(resIterator);

        if(res->getName().length() > 0) this->mNameToResourceMap.erase(res->getName());
        removeResourceFromTypedVector(res);
    }

    // helper methods for managing typed resource vectors
    void addResourceToTypedVector(Resource *res) {
        if(!res) return;

        switch(res->getResType()) {
            case Resource::Type::IMAGE:
                this->vImages.push_back(res->asImage());
                break;
            case Resource::Type::FONT:
                this->vFonts.push_back(res->asFont());
                break;
            case Resource::Type::SOUND:
                this->vSounds.push_back(res->asSound());
                break;
            case Resource::Type::SHADER:
                this->vShaders.push_back(res->asShader());
                break;
            case Resource::Type::RENDERTARGET:
                this->vRenderTargets.push_back(res->asRenderTarget());
                break;
            case Resource::Type::TEXTUREATLAS:
                this->vTextureAtlases.push_back(res->asTextureAtlas());
                break;
            case Resource::Type::VAO:
                this->vVertexArrayObjects.push_back(res->asVAO());
                break;
            case Resource::Type::APPDEFINED:
                // app-defined types aren't added to specific vectors
                break;
        }
    }

    void removeResourceFromTypedVector(Resource *res) {
        if(!res) return;

        switch(res->getResType()) {
            case Resource::Type::IMAGE: {
                std::erase(this->vImages, res);
            } break;
            case Resource::Type::FONT: {
                std::erase(this->vFonts, res);
            } break;
            case Resource::Type::SOUND: {
                std::erase(this->vSounds, res);
            } break;
            case Resource::Type::SHADER: {
                std::erase(this->vShaders, res);
            } break;
            case Resource::Type::RENDERTARGET: {
                std::erase(this->vRenderTargets, res);
            } break;
            case Resource::Type::TEXTUREATLAS: {
                std::erase(this->vTextureAtlases, res);
            } break;
            case Resource::Type::VAO: {
                std::erase(this->vVertexArrayObjects, res);
            } break;
            case Resource::Type::APPDEFINED:
                // app-defined types aren't added to specific vectors
                break;
        }
    }

    void setResourceName(Resource *res, std::string_view name) {
        if(!res) {
            logIfCV(debug_rm, "attempted to set name {:s} on NULL resource!", name);
            return;
        }

        std::string currentName = res->getName();
        if(!currentName.empty() && currentName == name) return;  // it's already the same name, nothing to do

        res->setName(name);

        // add the new name to the resource map (if it's a managed resource)
        if(this->nextLoadUnmanagedStack.size() < 1 || !this->nextLoadUnmanagedStack.back())
            this->mNameToResourceMap.try_emplace(name, res);
        return;
    }

    // content
    std::vector<Resource *> vResources;

    // typed resource vectors for fast type-specific access
    std::vector<Image *> vImages;
    std::vector<McFont *> vFonts;
    std::vector<Sound *> vSounds;
    std::vector<Shader *> vShaders;
    std::vector<RenderTarget *> vRenderTargets;
    std::vector<TextureAtlas *> vTextureAtlases;
    std::vector<VertexArrayObject *> vVertexArrayObjects;

    // lookup map
    Hash::unstable_stringmap<Resource *> mNameToResourceMap;

    // async loading system
    AsyncResourceLoader asyncLoader;

    // flags
    Sync::shared_mutex managedLoadMutex;
    std::vector<bool> nextLoadUnmanagedStack;
    std::atomic<bool> bNextLoadAsync;
};

ResourceManager::ResourceManager() : pImpl() /* create implementation */ {}
ResourceManager::~ResourceManager() {
    // release all not-currently-being-loaded resources
    destroyResources();

    // async loader shutdown handles thread cleanup
    pImpl->asyncLoader.shutdown();
}

// resource access by name
Image *ResourceManager::getImage(std::string_view resourceName) const { return pImpl->tryGet<Image>(resourceName); }
McFont *ResourceManager::getFont(std::string_view resourceName) const { return pImpl->tryGet<McFont>(resourceName); }
Sound *ResourceManager::getSound(std::string_view resourceName) const { return pImpl->tryGet<Sound>(resourceName); }
Shader *ResourceManager::getShader(std::string_view resourceName) const { return pImpl->tryGet<Shader>(resourceName); }

// methods for getting all resources of a type
const std::vector<Image *> &ResourceManager::getImages() const { return pImpl->vImages; }
const std::vector<McFont *> &ResourceManager::getFonts() const { return pImpl->vFonts; }
const std::vector<Sound *> &ResourceManager::getSounds() const { return pImpl->vSounds; }
const std::vector<Shader *> &ResourceManager::getShaders() const { return pImpl->vShaders; }
const std::vector<RenderTarget *> &ResourceManager::getRenderTargets() const { return pImpl->vRenderTargets; }
const std::vector<TextureAtlas *> &ResourceManager::getTextureAtlases() const { return pImpl->vTextureAtlases; }
const std::vector<VertexArrayObject *> &ResourceManager::getVertexArrayObjects() const {
    return pImpl->vVertexArrayObjects;
}
const std::vector<Resource *> &ResourceManager::getResources() const { return pImpl->vResources; }

void ResourceManager::update() {
    // delegate to async loader
    bool lowLatency = app->isInUnpausedGameplay();
    pImpl->asyncLoader.update(lowLatency);
}

void ResourceManager::destroyResources() {
    while(pImpl->vResources.size() > 0) {
        destroyResource(pImpl->vResources[0], ResourceDestroyFlags::RDF_FORCE_BLOCKING);
    }
    pImpl->vResources.clear();
    pImpl->vImages.clear();
    pImpl->vFonts.clear();
    pImpl->vSounds.clear();
    pImpl->vShaders.clear();
    pImpl->vRenderTargets.clear();
    pImpl->vTextureAtlases.clear();
    pImpl->vVertexArrayObjects.clear();
    pImpl->mNameToResourceMap.clear();
}

void ResourceManager::destroyResource(Resource *rs, ResourceDestroyFlags destflags) {
    using namespace flags::operators;

    const bool debug = cv::debug_rm.getBool();
    if(rs == nullptr) {
        logIf(debug, "W: destroyResource(NULL)!");
        return;
    }

    logIf(debug, "destroying {:s}", rs->getDebugIdentifier());

    auto managedResourceIterator = std::ranges::find(pImpl->vResources, rs);
    const bool isManagedResource = managedResourceIterator != pImpl->vResources.end();

    using enum ResourceDestroyFlags;
    const bool shouldDelete = !flags::has<RDF_NODELETE>(destflags);
    if(!shouldDelete) {
        // kind of ugly but otherwise race conditions galore if the resource gets used while it's also being destroyed asynchronously
        destflags |= RDF_FORCE_BLOCKING;
    }

    // check if it's being loaded and schedule async destroy if so
    // (!!(flags & ResourceDestroyFlags::RDF_FORCE_ASYNC)) ||
    if(pImpl->asyncLoader.isLoadingResource(rs)) {
        // interrupt async load
        rs->interruptLoad();

        if(isManagedResource) pImpl->removeManagedResource(rs, managedResourceIterator);

        if(flags::has<RDF_FORCE_BLOCKING>(destflags)) {
            pImpl->asyncLoader.waitForResource(rs);
            rs->release();
            if(shouldDelete) SAFE_DELETE(rs);
        } else {
            pImpl->asyncLoader.scheduleAsyncDestroy(rs, shouldDelete);
        }

        return;
    }

    // standard destroy
    if(isManagedResource) pImpl->removeManagedResource(rs, managedResourceIterator);

    rs->release();

    if(shouldDelete) {
        SAFE_DELETE(rs);
    }
}

void ResourceManager::loadResource(Resource *res, bool load) {
    if(res == nullptr) {
        logIfCV(debug_rm, "W: loadResource(NULL)!");
        pImpl->resetFlags();
        return;
    }

    // handle flags
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    bool isManaged;
    {
        Sync::shared_lock lock(pImpl->managedLoadMutex);
        isManaged = (pImpl->nextLoadUnmanagedStack.size() < 1 || !pImpl->nextLoadUnmanagedStack.back());
    }

    if(isManaged) pImpl->addManagedResource(res);

    const bool isNextLoadAsync = pImpl->bNextLoadAsync.load(std::memory_order_acquire);

    // flags must be reset on every load, to not carry over
    pImpl->resetFlags();

    if(!load) return;

    if(!isNextLoadAsync) {
        // load normally
        res->loadAsync();
        res->load();
    } else {
        // delegate to async loader
        pImpl->asyncLoader.requestAsyncLoad(res);
    }
}

bool ResourceManager::isLoading() const { return pImpl->asyncLoader.isLoading(); }

bool ResourceManager::isLoadingResource(const Resource *rs) const { return pImpl->asyncLoader.isLoadingResource(rs); }

bool ResourceManager::waitForResource(Resource *rs) { return pImpl->asyncLoader.waitForResource(rs); }

size_t ResourceManager::getNumInFlight() const { return pImpl->asyncLoader.getNumInFlight(); }

size_t ResourceManager::getNumAsyncDestroyQueue() const { return pImpl->asyncLoader.getNumAsyncDestroyQueue(); }

void ResourceManager::requestNextLoadAsync() { pImpl->bNextLoadAsync.store(true, std::memory_order_release); }

void ResourceManager::requestNextLoadUnmanaged() {
    Sync::unique_lock lock(pImpl->managedLoadMutex);
    pImpl->nextLoadUnmanagedStack.push_back(true);
}

size_t ResourceManager::getSyncLoadMaxBatchSize() const { return pImpl->asyncLoader.getMaxPerUpdate(); }

void ResourceManager::setSyncLoadMaxBatchSize(size_t resourcesToLoad) {
    pImpl->asyncLoader.setMaxPerUpdate(resourcesToLoad);
}

void ResourceManager::reloadResource(Resource *rs, bool async) {
    if(rs == nullptr) {
        logIfCV(debug_rm, "W: reloadResource(NULL)!");
        return;
    }

    const std::vector<Resource *> resourceToReload{rs};
    reloadResources(resourceToReload, async);
}

void ResourceManager::reloadResources(const std::vector<Resource *> &resources, bool async) {
    if(resources.empty()) {
        logIfCV(debug_rm, "W: reloadResources with an empty resources vector!");
        return;
    }

    if(!async)  // synchronous
    {
        for(auto &res : resources) {
            if(unlikely(pImpl->asyncLoader.isLoadingResource(res))) {
                res->interruptLoad();
                pImpl->asyncLoader.waitForResource(res);
            }
            res->reload();
        }

        return;
    }

    // delegate to async loader
    pImpl->asyncLoader.reloadResources(resources);
}

bool ResourceManager::addManagedResource(Resource *userPtr, const std::string &resourceName) {
    auto res = pImpl->checkIfExistsAndHandle<Resource>(resourceName);
    if(res != nullptr) return false;

    // clear next load unmanaged
    pImpl->resetFlags();

    // set it up
    pImpl->setResourceName(userPtr, resourceName);
    loadResource(userPtr, false);

    return true;
}

Image *ResourceManager::loadImage(std::string filepath, const std::string &resourceName, bool mipmapped,
                                  bool keepInSystemMemory) {
    auto res = pImpl->checkIfExistsAndHandle<Image>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    filepath.insert(0, MCENGINE_IMAGES_PATH "/");
    Image *img = g->createImage(filepath, mipmapped, keepInSystemMemory);
    pImpl->setResourceName(img, resourceName);

    loadResource(img, true);

    return img;
}

Image *ResourceManager::loadImageUnnamed(std::string filepath, bool mipmapped, bool keepInSystemMemory) {
    filepath.insert(0, MCENGINE_IMAGES_PATH "/");
    Image *img = g->createImage(filepath, mipmapped, keepInSystemMemory);

    loadResource(img, true);

    return img;
}

Image *ResourceManager::loadImageAbs(std::string absoluteFilepath, const std::string &resourceName, bool mipmapped,
                                     bool keepInSystemMemory) {
    auto res = pImpl->checkIfExistsAndHandle<Image>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    Image *img = g->createImage(std::move(absoluteFilepath), mipmapped, keepInSystemMemory);
    pImpl->setResourceName(img, resourceName);

    loadResource(img, true);

    return img;
}

Image *ResourceManager::loadImageAbsUnnamed(std::string absoluteFilepath, bool mipmapped, bool keepInSystemMemory) {
    Image *img = g->createImage(std::move(absoluteFilepath), mipmapped, keepInSystemMemory);

    loadResource(img, true);

    return img;
}

Image *ResourceManager::createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) {
    if(width > 8192 || height > 8192) {
        engine->showMessageError("Resource Manager Error", fmt::format("Invalid parameters in createImage({}, {}, {})!",
                                                                       width, height, (int)mipmapped));
        return nullptr;
    }

    Image *img = g->createImage(width, height, mipmapped, keepInSystemMemory);

    loadResource(img, false);

    return img;
}

McFont *ResourceManager::loadFont(std::string filepath, const std::string &resourceName, int fontSize,
                                  bool antialiasing, int fontDPI) {
    auto res = pImpl->checkIfExistsAndHandle<McFont>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    filepath.insert(0, MCENGINE_FONTS_PATH "/");
    auto *fnt = new McFont(std::move(filepath), fontSize, antialiasing, fontDPI);
    pImpl->setResourceName(fnt, resourceName);

    loadResource(fnt, true);

    return fnt;
}

McFont *ResourceManager::loadFont(std::string filepath, const std::string &resourceName,
                                  const std::span<const char16_t> &characters, int fontSize, bool antialiasing,
                                  int fontDPI) {
    auto res = pImpl->checkIfExistsAndHandle<McFont>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    filepath.insert(0, MCENGINE_FONTS_PATH "/");
    auto *fnt = new McFont(std::move(filepath), characters, fontSize, antialiasing, fontDPI);
    pImpl->setResourceName(fnt, resourceName);

    loadResource(fnt, true);

    return fnt;
}

Sound *ResourceManager::loadSound(std::string filepath, const std::string &resourceName, bool stream, bool overlayable,
                                  bool loop) {
    auto res = pImpl->checkIfExistsAndHandle<Sound>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    filepath.insert(0, MCENGINE_SOUNDS_PATH "/");
    auto *snd{soundEngine->createSound(filepath, stream, overlayable, loop)};
    pImpl->setResourceName(snd, resourceName);

    loadResource(snd, true);

    return snd;
}

Sound *ResourceManager::loadSoundAbs(std::string filepath, const std::string &resourceName, bool stream,
                                     bool overlayable, bool loop) {
    auto res = pImpl->checkIfExistsAndHandle<Sound>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    auto *snd{soundEngine->createSound(std::move(filepath), stream, overlayable, loop)};
    pImpl->setResourceName(snd, resourceName);

    loadResource(snd, true);

    return snd;
}

Shader *ResourceManager::loadShader(std::string vertexShaderFilePath, std::string fragmentShaderFilePath,
                                    const std::string &resourceName) {
    auto res = pImpl->checkIfExistsAndHandle<Shader>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    vertexShaderFilePath.insert(0, MCENGINE_SHADERS_PATH "/");
    fragmentShaderFilePath.insert(0, MCENGINE_SHADERS_PATH "/");
    Shader *shader = g->createShaderFromFile(vertexShaderFilePath, fragmentShaderFilePath);
    pImpl->setResourceName(shader, resourceName);

    loadResource(shader, true);

    return shader;
}

Shader *ResourceManager::loadShader(std::string vertexShaderFilePath, std::string fragmentShaderFilePath) {
    vertexShaderFilePath.insert(0, MCENGINE_SHADERS_PATH "/");
    fragmentShaderFilePath.insert(0, MCENGINE_SHADERS_PATH "/");
    Shader *shader = g->createShaderFromFile(vertexShaderFilePath, fragmentShaderFilePath);

    loadResource(shader, true);

    return shader;
}

Shader *ResourceManager::createShader(std::string vertexShader, std::string fragmentShader,
                                      const std::string &resourceName) {
    auto res = pImpl->checkIfExistsAndHandle<Shader>(resourceName);
    if(res != nullptr) return res;

    // create instance and load it
    Shader *shader = g->createShaderFromSource(std::move(vertexShader), std::move(fragmentShader));
    pImpl->setResourceName(shader, resourceName);

    loadResource(shader, true);

    return shader;
}

Shader *ResourceManager::createShader(std::string vertexShader, std::string fragmentShader) {
    Shader *shader = g->createShaderFromSource(std::move(vertexShader), std::move(fragmentShader));

    loadResource(shader, true);

    return shader;
}

Shader *ResourceManager::createShaderAuto(std::string_view shaderBasename) {
    auto *shader = pImpl->checkIfExistsAndHandle<Shader>(shaderBasename);
    if(shader != nullptr) return shader;

    // create instance and load it
    const std::string_view pfx = env->usingDX11() ? "DX11" : env->usingSDLGPU() ? "SDLGPU" : "GL";
    assert(ALL_BINMAP.contains(fmt::format("{}_{}_vsh", pfx, shaderBasename)) &&
           ALL_BINMAP.contains(fmt::format("{}_{}_fsh", pfx, shaderBasename)));
    shader = g->createShaderFromSource(std::string{ALL_BINMAP.at(fmt::format("{}_{}_vsh", pfx, shaderBasename))},
                                       std::string{ALL_BINMAP.at(fmt::format("{}_{}_fsh", pfx, shaderBasename))});

    pImpl->setResourceName(shader, shaderBasename);
    loadResource(shader, true);

    return shader;
}

RenderTarget *ResourceManager::createRenderTarget(int x, int y, int width, int height,
                                                  MultisampleType multiSampleType) {
    RenderTarget *rt = g->createRenderTarget(x, y, width, height, multiSampleType);
    // for uniqueness, use timestamp
    pImpl->setResourceName(rt, fmt::format("_RT_{:d}x{:d}-{}", width, height, Timing::getTicksNS()));

    loadResource(rt, true);

    return rt;
}

RenderTarget *ResourceManager::createRenderTarget(int width, int height, MultisampleType multiSampleType) {
    return createRenderTarget(0, 0, width, height, multiSampleType);
}

TextureAtlas *ResourceManager::createTextureAtlas(int width, int height, bool filtering) {
    auto *ta = new TextureAtlas(width, height, filtering);
    pImpl->setResourceName(ta, fmt::format("_TA_{:d}x{:d}-{}", width, height, Timing::getTicksNS()));

    loadResource(ta, false);

    return ta;
}

VertexArrayObject *ResourceManager::createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                            bool keepInSystemMemory) {
    VertexArrayObject *vao = g->createVertexArrayObject(primitive, usage, keepInSystemMemory);

    loadResource(vao, false);

    return vao;
}
