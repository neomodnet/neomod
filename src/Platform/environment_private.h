#pragma once
// Copyright (c) 2025, WH, All rights reserved.

#include "Environment.h"

#if !defined(SDL_h_) && !defined(SDL_main_h_)
extern "C" {
typedef struct SDL_GLContextState *SDL_GLContext;
typedef union SDL_Event SDL_Event;
enum SDL_AppResult : unsigned int;
}
#endif

#if !(defined(SDL_main_h_) && defined(MCENGINE_FEATURE_MAINCALLBACKS))
extern void SDL_AppQuit(void *appstate, SDL_AppResult result);
#endif

class GPUDriverConfigurator;
namespace Mc {
struct AppDescriptor;
}

class SDLMain final : public Environment {
    NOCOPY_NOMOVE(SDLMain)
    friend void SDL_AppQuit(void *appstate, SDL_AppResult result);

   public:
    SDLMain(const Mc::AppDescriptor &appDesc);
    ~SDLMain() override;

    SDL_AppResult initialize();
    SDL_AppResult iterate();
    SDL_AppResult handleEvent(SDL_Event *event);
    void shutdown(SDL_AppResult result);

    static void restart();

   private:
    // init methods
    bool createWindow();
    void setupLogging();
    void configureEvents();
    float queryDisplayHz();

    // callback handlers
    void fps_max_callback(float newVal);
    void fps_max_background_callback(float newVal);

    // set iteration rate for callbacks
    void setFgFPS();
    void setBgFPS();

    // no way to query refresh rate in WASM so do it by measuring a few frames on startup
    [[maybe_unused]] void calibrateDisplayHzWASM();

    // dpi update callback
    void onDPIChange();

    // for live resizing on windows
    // SDL will call this on the main thread when the window needs to be redrawn
    static bool resizeCallback(void *userdata, SDL_Event *event);

    // debug convar callbacks
    void sendkey(std::string_view args);
    void sendtext(std::string_view args);

    void pushMouseButtonEvent(std::string_view btnName, bool down);
    void mouse_to(std::string_view args);
    void mouse_down(std::string_view args);
    void mouse_up(std::string_view args);
    void mouse_wheel(std::string_view args);

    // GL context (must be created early, during window creation)
    SDL_GLContext m_context{nullptr};

    int m_iFpsMax{360};
    int m_iFpsMaxBG{30};

    std::vector<std::string> m_vDroppedData;  // queued data dropped onto window

    friend class GPUDriverConfigurator;
    std::unique_ptr<GPUDriverConfigurator> m_gpuConfigurator;

    // for profiling with main callbacks
    bool m_bInIterate{false};

    // WASM: measure display Hz from rAF frame intervals
    uint64_t m_iHzMeasureStartNS{0};
    int m_iHzMeasureFrames{-1};  // -1 = waiting for init delay, 0..N = measuring, N = done

    static constexpr const int WASM_HZ_FRAMES_TO_MEASURE{50};
    static constexpr const int WASM_HZ_INIT_DELAY_SECONDS{5};
};
