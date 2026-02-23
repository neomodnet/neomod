#pragma once
// Copyright (c) 2012, PG, All rights reserved.

#ifndef RESOURCE_H
#define RESOURCE_H

#include "BaseEnvironment.h"

#include "fmt/format.h"
#include "fmt/compile.h"

#include <atomic>
#include <string>
#include <memory>
#include <utility>  // unreachable
#include <optional>

class TextureAtlas;
class Sound;
class McFont;
class Image;
class Shader;
class VertexArrayObject;
class RenderTarget;

class Resource {
    NOCOPY_NOMOVE(Resource)
    friend class ResourceManager;
    friend struct ResourceManagerImpl;

   public:
    enum Type : uint8_t {
        IMAGE,
        FONT,
        RENDERTARGET,
        SHADER,
        TEXTUREATLAS,
        VAO,
        SOUND,
        APPDEFINED,
        LAST_RESTYPE = APPDEFINED
    };

   protected:
    constexpr Resource(Type resType) : resType(resType) {
        this->sDebugIdentifier.assign(fmt::format(
            fmt::operator""_cf < "{:8p}:{:s}:name=<none>:postinit=false:filepath=<none>">(), fmt::ptr(this),
            this->typeToString()));
    }

    Resource(Type resType, std::string filepath, bool doFilesystemExistenceCheck = true);

   public:
    Resource() = delete;
    virtual ~Resource() = default;

    void load();
    void loadAsync();
    void release();
    void reload();
    virtual void interruptLoad();

    [[nodiscard]] inline const std::string &getName() const { return this->sName; }
    [[nodiscard]] inline const std::string &getFilePath() const { return this->sFilePath; }
    [[nodiscard]] inline const std::string &getDebugIdentifier() const { return this->sDebugIdentifier; }

    [[nodiscard]] forceinline bool isReady() const { return this->bReady.load(std::memory_order_acquire); }
    [[nodiscard]] forceinline bool isAsyncReady() const { return this->bAsyncReady.load(std::memory_order_acquire); }
    [[nodiscard]] forceinline bool isInterrupted() const { return this->bInterrupted.load(std::memory_order_acquire); }

    // run a callback after init() is called, rs parameter is the resource that finished loading
    // check rs->isReady() for success status
    struct SyncLoadCB {
        using cb = void (*)(Resource *rs, void *userdata);
        void *userdata;
        cb callback;
    };

    inline void setOnInitCB(std::optional<SyncLoadCB> callback = std::nullopt) {
        // arg can be empty to remove
        this->onInit = callback;
    }

   protected:
    virtual void init() = 0;
    virtual void initAsync() = 0;
    virtual void destroy() = 0;
    static bool doPathFixup(std::string &input);

    inline void setReady(bool ready) { return this->bReady.store(ready, std::memory_order_release); }
    inline void setAsyncReady(bool ready) { return this->bAsyncReady.store(ready, std::memory_order_release); }

    std::optional<SyncLoadCB> onInit;
    std::string sFilePath{};
    std::string sName{};
    std::string sDebugIdentifier;

    std::atomic<bool> bReady{false};
    std::atomic<bool> bAsyncReady{false};
    std::atomic<bool> bInterrupted{false};

    Type resType;

   public:
    // type inspection
    [[nodiscard]] constexpr Type getResType() const { return this->resType; }

    template <typename T = Resource>
    T *as() {
        if constexpr(std::is_same_v<T, Resource>)
            return this;
        else if constexpr(std::is_same_v<T, Image>)
            return this->asImage();
        else if constexpr(std::is_same_v<T, McFont>)
            return this->asFont();
        else if constexpr(std::is_same_v<T, RenderTarget>)
            return this->asRenderTarget();
        else if constexpr(std::is_same_v<T, TextureAtlas>)
            return this->asTextureAtlas();
        else if constexpr(std::is_same_v<T, Shader>)
            return this->asShader();
        else if constexpr(std::is_same_v<T, VertexArrayObject>)
            return this->asVAO();
        else if constexpr(std::is_same_v<T, Sound>)
            return this->asSound();
        else if constexpr(std::is_same_v<T, const Resource>)
            return static_cast<const Resource *>(this);
        else if constexpr(std::is_same_v<T, const Image>)
            return static_cast<const Image *>(this->asImage());
        else if constexpr(std::is_same_v<T, const McFont>)
            return static_cast<const McFont *>(this->asFont());
        else if constexpr(std::is_same_v<T, const RenderTarget>)
            return static_cast<const RenderTarget *>(this->asRenderTarget());
        else if constexpr(std::is_same_v<T, const TextureAtlas>)
            return static_cast<const TextureAtlas *>(this->asTextureAtlas());
        else if constexpr(std::is_same_v<T, const Shader>)
            return static_cast<const Shader *>(this->asShader());
        else if constexpr(std::is_same_v<T, const VertexArrayObject>)
            return static_cast<const VertexArrayObject *>(this->asVAO());
        else if constexpr(std::is_same_v<T, const Sound>)
            return static_cast<const Sound *>(this->asSound());
        else
            static_assert(Env::always_false_v<T>, "unsupported type for resource");
        return nullptr;
    }
    virtual Image *asImage() { return nullptr; }
    virtual McFont *asFont() { return nullptr; }
    virtual RenderTarget *asRenderTarget() { return nullptr; }
    virtual Shader *asShader() { return nullptr; }
    virtual TextureAtlas *asTextureAtlas() { return nullptr; }
    virtual VertexArrayObject *asVAO() { return nullptr; }
    virtual Sound *asSound() { return nullptr; }
    [[nodiscard]] const virtual Image *asImage() const { return nullptr; }
    [[nodiscard]] const virtual McFont *asFont() const { return nullptr; }
    [[nodiscard]] const virtual RenderTarget *asRenderTarget() const { return nullptr; }
    [[nodiscard]] const virtual Shader *asShader() const { return nullptr; }
    [[nodiscard]] const virtual TextureAtlas *asTextureAtlas() const { return nullptr; }
    [[nodiscard]] const virtual VertexArrayObject *asVAO() const { return nullptr; }
    [[nodiscard]] const virtual Sound *asSound() const { return nullptr; }

    [[nodiscard]] constexpr std::string_view typeToString() const {
        static_assert(APPDEFINED == LAST_RESTYPE);

        switch(this->getResType()) {
            case IMAGE:
                return "IMAGE";
            case FONT:
                return "FONT";
            case RENDERTARGET:
                return "RENDERTARGET";
            case SHADER:
                return "SHADER";
            case TEXTUREATLAS:
                return "TEXTUREATLAS";
            case VAO:
                return "VAO";
            case SOUND:
                return "SOUND";
            case APPDEFINED:
                return "APPDEFINED";
        }
        std::unreachable();
    }

   private:
    // used by resourcemanager
    void setName(std::string_view name);
};

#endif
