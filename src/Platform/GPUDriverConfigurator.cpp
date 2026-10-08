// Copyright (c) 2025, WH, All rights reserved.
#include "GPUDriverConfigurator.h"
#include "ConVar.h"
#include "Logging.h"
#include "MakeDelegateWrapper.h"
#include "dynutils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>

#if defined(MCENGINE_PLATFORM_WINDOWS) && defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x602
#include "WinDebloatDefs.h"
#include <windows.h>

extern "C" {  // switch to the high performance gpu in multi-gpu systems (mostly laptops)
__declspec(dllexport) DWORD NvOptimusEnablement =
    0x00000001;  // http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance =
    0x00000001;  // https://community.amd.com/thread/169965
}
#endif

namespace {  // static namespace

// NvAPI status codes
enum class NvStatus : int32_t {
    OK = 0,
    ERROR = -1,
    LIBRARY_NOT_FOUND = -2,
    API_NOT_INITIALIZED = -4,
    INVALID_ARGUMENT = -5,
    NVIDIA_DEVICE_NOT_FOUND = -6,
    INVALID_HANDLE = -8,
    INCOMPATIBLE_STRUCT_VERSION = -9,
    SETTING_NOT_FOUND = -160,
    PROFILE_NOT_FOUND = -163,
    EXECUTABLE_NOT_FOUND = -166,
};

// NvAPI setting identifiers
enum class NvSettingId : uint32_t {
    OGL_THREAD_CONTROL_ID = 0x20C1221E,
};

enum class NvSettingType : uint32_t {
    DWORD = 0,
    BINARY = 1,
    STRING = 2,
    WSTRING = 3,
};

enum class NvSettingLocation : uint32_t {
    CURRENT = 0,
    GLOBAL = 1,
    BASE = 2,
    DEFAULT = 3,
};

// NvAPI structures (must match exact memory layout)
#pragma pack(push, 8)

struct NvDrsProfileV1 {
    uint32_t version;
    wchar_t profileName[0x800];
    uint32_t gpuSupport;
    uint32_t isPredefined;
    uint32_t numOfApps;
    uint32_t numOfSettings;

    static constexpr uint32_t makeVersion() noexcept { return sizeof(NvDrsProfileV1) | (1 << 16); }
};

struct NvDrsApplicationV1 {
    uint32_t version;
    uint32_t isPredefined;
    wchar_t appName[0x800];
    wchar_t userFriendlyName[0x800];
    wchar_t launcher[0x800];

    static constexpr uint32_t makeVersion() noexcept { return sizeof(NvDrsApplicationV1) | (1 << 16); }
};

struct NvDrsSettingValue {
    uint32_t u32Value;
    uint8_t pad[0x1000];
};

struct NvDrsSettingV1 {
    uint32_t version;
    wchar_t settingName[2048];
    NvSettingId settingId;
    NvSettingType settingType;
    NvSettingLocation settingLocation;
    uint32_t isCurrentPredefined;
    uint32_t isPredefinedValid;
    NvDrsSettingValue predefinedValue;
    NvDrsSettingValue currentValue;

    static constexpr uint32_t makeVersion() noexcept { return sizeof(NvDrsSettingV1) | (1 << 16); }
};

#pragma pack(pop)

// NvAPI_QueryInterface function pointer types
using NvAPI_QueryInterface_t = void *(uint32_t id);
using NvAPI_Initialize_t = NvStatus();
using NvAPI_DRS_CreateSession_t = NvStatus(void **sessionHandle);
using NvAPI_DRS_DestroySession_t = NvStatus(void *sessionHandle);
using NvAPI_DRS_LoadSettings_t = NvStatus(void *sessionHandle);
using NvAPI_DRS_FindApplicationByName_t = NvStatus(void *sessionHandle, const wchar_t *appName, void **profileHandle,
                                                   NvDrsApplicationV1 *application);
using NvAPI_DRS_CreateProfile_t = NvStatus(void *sessionHandle, NvDrsProfileV1 *profile, void **profileHandle);
using NvAPI_DRS_CreateApplication_t = NvStatus(void *sessionHandle, void *profileHandle, NvDrsApplicationV1 *app);
using NvAPI_DRS_SetSetting_t = NvStatus(void *sessionHandle, void *profileHandle, NvDrsSettingV1 *setting);
using NvAPI_DRS_SaveSettings_t = NvStatus(void *sessionHandle);

// NvAPI_QueryInterface function interface IDs
constexpr uint32_t NVAPI_INITIALIZE_ID = 0x0150E828;
constexpr uint32_t NVAPI_DRS_CREATE_SESSION_ID = 0x0694D52E;
constexpr uint32_t NVAPI_DRS_DESTROY_SESSION_ID = 0xDAD9CFF8;
constexpr uint32_t NVAPI_DRS_LOAD_SETTINGS_ID = 0x375DBD6B;
constexpr uint32_t NVAPI_DRS_FIND_APPLICATION_BY_NAME_ID = 0xEEE566B2;
constexpr uint32_t NVAPI_DRS_CREATE_PROFILE_ID = 0xCC176068;
constexpr uint32_t NVAPI_DRS_CREATE_APPLICATION_ID = 0x4347A9DE;
constexpr uint32_t NVAPI_DRS_SET_SETTING_ID = 0x577DD202;
constexpr uint32_t NVAPI_DRS_SAVE_SETTINGS_ID = 0xFCBC7E14;

struct NvApiState {
    dynutils::lib_obj *lib{nullptr};
    NvAPI_QueryInterface_t *queryInterface{nullptr};
    NvAPI_Initialize_t *initialize{nullptr};
    NvAPI_DRS_CreateSession_t *createSession{nullptr};
    NvAPI_DRS_DestroySession_t *destroySession{nullptr};
    NvAPI_DRS_LoadSettings_t *loadSettings{nullptr};
    NvAPI_DRS_FindApplicationByName_t *findApplicationByName{nullptr};
    NvAPI_DRS_CreateProfile_t *createProfile{nullptr};
    NvAPI_DRS_CreateApplication_t *createApplication{nullptr};
    NvAPI_DRS_SetSetting_t *setSetting{nullptr};
    NvAPI_DRS_SaveSettings_t *saveSettings{nullptr};
    bool initialized{false};
};

std::string s_init_info{""};
static CONSTINIT NvApiState s_state{};

[[maybe_unused]] bool initNvAPI() noexcept {
    if(s_state.initialized) return true;

    static constexpr const char *nvapi_libname = (sizeof(void *) == 8) ? "nvapi64.dll" : "nvapi.dll";
    s_state.lib = dynutils::load_lib(nvapi_libname);

    if(!s_state.lib) {
        s_init_info = fmt::format("NOTICE: failed to load {}: {}", nvapi_libname, dynutils::get_error());
        return false;
    }

    // load NvAPI_QueryInterface function
    s_state.queryInterface = dynutils::load_func<NvAPI_QueryInterface_t>(s_state.lib, "nvapi_QueryInterface");
    if(!s_state.queryInterface) {
        s_init_info = "NOTICE: failed to load nvapi_QueryInterface (can't set threaded optimizations setting)";
        return false;
    }

    // load function pointers through QueryInterface
    s_state.initialize = reinterpret_cast<NvAPI_Initialize_t *>(s_state.queryInterface(NVAPI_INITIALIZE_ID));
    s_state.createSession =
        reinterpret_cast<NvAPI_DRS_CreateSession_t *>(s_state.queryInterface(NVAPI_DRS_CREATE_SESSION_ID));
    s_state.destroySession =
        reinterpret_cast<NvAPI_DRS_DestroySession_t *>(s_state.queryInterface(NVAPI_DRS_DESTROY_SESSION_ID));
    s_state.loadSettings =
        reinterpret_cast<NvAPI_DRS_LoadSettings_t *>(s_state.queryInterface(NVAPI_DRS_LOAD_SETTINGS_ID));
    s_state.findApplicationByName = reinterpret_cast<NvAPI_DRS_FindApplicationByName_t *>(
        s_state.queryInterface(NVAPI_DRS_FIND_APPLICATION_BY_NAME_ID));
    s_state.createProfile =
        reinterpret_cast<NvAPI_DRS_CreateProfile_t *>(s_state.queryInterface(NVAPI_DRS_CREATE_PROFILE_ID));
    s_state.createApplication =
        reinterpret_cast<NvAPI_DRS_CreateApplication_t *>(s_state.queryInterface(NVAPI_DRS_CREATE_APPLICATION_ID));
    s_state.setSetting = reinterpret_cast<NvAPI_DRS_SetSetting_t *>(s_state.queryInterface(NVAPI_DRS_SET_SETTING_ID));
    s_state.saveSettings =
        reinterpret_cast<NvAPI_DRS_SaveSettings_t *>(s_state.queryInterface(NVAPI_DRS_SAVE_SETTINGS_ID));

    if(!s_state.initialize || !s_state.createSession || !s_state.destroySession || !s_state.loadSettings ||
       !s_state.findApplicationByName || !s_state.createProfile || !s_state.createApplication || !s_state.setSetting ||
       !s_state.saveSettings) {
        s_init_info = "NOTICE: failed to load one or more NvAPI functions (can't set threaded optimizations setting)";
        return false;
    }

    // initialize NvAPI
    NvStatus status = s_state.initialize();
    if(status != NvStatus::OK) {
        s_init_info =
            fmt::format("NOTICE: NvAPI_Initialize failed with status {} (can't set threaded optimizations setting)",
                        static_cast<int>(status));
        return false;
    }

    s_state.initialized = true;

    if constexpr(Env::cfg(BUILD::DEBUG)) {
        s_init_info = "NvAPI initialized";
    }

    return true;
}

void unloadNvAPI() noexcept {
    if constexpr(!Env::cfg(OS::WINDOWS)) return;
    if(!s_state.initialized || !s_state.lib) return;

    dynutils::unload_lib(s_state.lib);
}

bool setNvidiaThreadedOpts(bool enable) noexcept {
    if(!s_state.initialized) return false;

    void *sessionHandle = nullptr;
    NvStatus status = s_state.createSession(&sessionHandle);
    if(status != NvStatus::OK) {
        debugLog("couldn't {} threaded opts: NvAPI_DRS_CreateSession failed with status {}",
                 enable ? "enable" : "disable", static_cast<int>(status));
        return false;
    }

    // load current settings
    status = s_state.loadSettings(sessionHandle);
    if(status != NvStatus::OK) {
        debugLog("couldn't {} threaded opts: NvAPI_DRS_LoadSettings failed with status {}",
                 enable ? "enable" : "disable", static_cast<int>(status));
        s_state.destroySession(sessionHandle);
        return false;
    }

    // try to find existing profile for this application
    NvDrsApplicationV1 app{};
    app.version = NvDrsApplicationV1::makeVersion();

    void *profileHandle = nullptr;
    status = s_state.findApplicationByName(sessionHandle, L"" PACKAGE_NAME ".exe", &profileHandle, &app);

    if(status == NvStatus::EXECUTABLE_NOT_FOUND) {
        // create new profile
        NvDrsProfileV1 profile{};
        profile.version = NvDrsProfileV1::makeVersion();

        std::wcsncpy(&profile.profileName[0], L"" PACKAGE_NAME,
                     (sizeof(profile.profileName) / sizeof(profile.profileName[0]) - 1));

        profile.isPredefined = 0;

        status = s_state.createProfile(sessionHandle, &profile, &profileHandle);
        if(status != NvStatus::OK) {
            debugLog("couldn't {} threaded opts: NvAPI_DRS_CreateProfile failed with status {}",
                     enable ? "enable" : "disable", static_cast<int>(status));
            s_state.destroySession(sessionHandle);
            return false;
        }

        // create application entry
        app.version = NvDrsApplicationV1::makeVersion();
        app.isPredefined = 0;
        std::wcsncpy(&app.appName[0], L"" PACKAGE_NAME ".exe", (sizeof(app.appName) / sizeof(app.appName[0])) - 1);
        std::wcsncpy(&app.userFriendlyName[0], L"" PACKAGE_NAME,
                     (sizeof(app.userFriendlyName) / sizeof(app.userFriendlyName[0])) - 1);

        status = s_state.createApplication(sessionHandle, profileHandle, &app);
        if(status != NvStatus::OK) {
            debugLog("couldn't {} threaded opts: NvAPI_DRS_CreateApplication failed with status {}",
                     enable ? "enable" : "disable", static_cast<int>(status));
            s_state.destroySession(sessionHandle);
            return false;
        }

        if constexpr(Env::cfg(BUILD::DEBUG)) {
            debugLog("created new NvAPI profile for {}", PACKAGE_NAME);
        }

    } else if(status != NvStatus::OK) {
        debugLog("couldn't {} threaded opts: NvAPI_DRS_FindApplicationByName failed with status {}",
                 enable ? "enable" : "disable", static_cast<int>(status));
        s_state.destroySession(sessionHandle);
        return false;
    }

    // set threaded optimization setting
    // value: 0 = auto (driver default), 1 = on, 2 = off
    // NOTE: intentionally only allowing "auto" or "forced disabled"
    // if the user wants to explicitly enable it in the nvidia control panel that's on them
    const uint32_t settingValue = enable ? 0 : 2;

    NvDrsSettingV1 setting{};
    setting.version = NvDrsSettingV1::makeVersion();
    setting.settingId = NvSettingId::OGL_THREAD_CONTROL_ID;
    setting.settingType = NvSettingType::DWORD;
    setting.settingLocation = NvSettingLocation::CURRENT;
    setting.currentValue.u32Value = settingValue;
    setting.predefinedValue.u32Value = settingValue;

    status = s_state.setSetting(sessionHandle, profileHandle, &setting);
    if(status != NvStatus::OK) {
        debugLog("couldn't {} threaded opts: NvAPI_DRS_SetSetting failed with status {}", enable ? "enable" : "disable",
                 static_cast<int>(status));
        s_state.destroySession(sessionHandle);
        return false;
    }

    // save settings
    status = s_state.saveSettings(sessionHandle);
    if(status != NvStatus::OK) {
        debugLog("couldn't {} threaded opts: NvAPI_DRS_SaveSettings failed with status {}",
                 enable ? "enable" : "disable", static_cast<int>(status));
        s_state.destroySession(sessionHandle);
        return false;
    }

    s_state.destroySession(sessionHandle);

    if constexpr(Env::cfg(BUILD::DEBUG)) {
        debugLog("nvidia threaded optimizations {}", enable ? "enabled" : "disabled");
    }
    return true;
}

}  // namespace

std::string_view GPUDriverConfigurator::getInitInfo() const noexcept { return s_init_info; }

GPUDriverConfigurator::GPUDriverConfigurator() noexcept
    : currently_disabled(cv::r_disable_driver_threaded_opts.getBool()) {
    // only supported on windows through NVAPI currently
    if constexpr(Env::cfg(OS::WINDOWS)) {
        // initialize NvAPI on construction
        if(initNvAPI()) {
            // apply initial setting
            if(!setNvidiaThreadedOpts(!this->currently_disabled)) {
                this->currently_disabled = !this->currently_disabled;
                cv::r_disable_driver_threaded_opts.setValue(this->currently_disabled);
                // update default so we don't save it to config
                cv::r_disable_driver_threaded_opts.setDefaultDouble(this->currently_disabled ? 1. : 0.);
            }

            cv::r_disable_driver_threaded_opts.setCallback(
                SA::MakeDelegate<&GPUDriverConfigurator::onDisableDrvThrdOptsChange>(this));
        }
    }
}

GPUDriverConfigurator::~GPUDriverConfigurator() noexcept {
    cv::r_disable_driver_threaded_opts.removeAllCallbacks();
    unloadNvAPI();
}

void GPUDriverConfigurator::onDisableDrvThrdOptsChange(float newVal) {
    const bool disable = !!static_cast<int>(newVal);
    if(disable != this->currently_disabled) {
        const bool succeeded = setNvidiaThreadedOpts(!disable);
        if(succeeded) {
            this->currently_disabled = disable;
        } else {
            // revert convar value on failure
            cv::r_disable_driver_threaded_opts.setValue(this->currently_disabled,
                                                        false /* don't run callback (recursively) */);
            // don't save the setting to config if we failed
            cv::r_disable_driver_threaded_opts.setDefaultDouble(this->currently_disabled ? 1. : 0.);
        }
    }
}
