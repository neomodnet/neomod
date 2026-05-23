// Copyright (c) 2026, WH, All rights reserved.
#include "BaseEnvironment.h"
#include "MainMenuTips.h"
#include "CBaseUILabel.h"
#include "ConVar.h"
#include "Environment.h"
#include "Font.h"
#include "i18n.h"

#include "OsuKeyBinds.h"
#include "Bancho.h"

#include "fmt/format.h"

#include <array>
#include <memory>

namespace cv {
extern ConVar main_menu_last_tip_index;
}

namespace neomod::mainmenu {
namespace {
// using std::string_literals::operator""s;
// using std::string_view_literals::operator""sv;

// allow dynamic and static tips
using DynamicTipFunc = std::function<std::string()>;
struct Tip {
    Tip(std::string_view str) : staticString(str) {}
    Tip(DynamicTipFunc tipReturningFunc) : dynamicTipFunc(new DynamicTipFunc{std::move(tipReturningFunc)}) {}

    [[nodiscard]] std::string get() const { return dynamicTipFunc ? (*dynamicTipFunc)() : std::string{staticString}; }

   private:
    std::string_view staticString;
    std::unique_ptr<DynamicTipFunc> dynamicTipFunc{nullptr};
};

std::span<const Tip> getAllTips() {
    // clang-format off
// NOTE: must have at least 1 non-empty tip in here
static std::array s_tips{
    Tip{_(R"(Press Alt + Enter to toggle Fullscreen.)")},
    Tip{_(R"(Press Shift + F1 to open the in-game console.)")},
    Tip{_(R"(Shift + Click on a skin in the dropdown list to change the source for base skin elements.)")},
    Tip{
        [&b = binds::TOGGLE_MODSELECT]() -> std::string
        { return tformat(R"(Press {:s} during gameplay to change mods in realtime.)", env->scanCodeToString(b.get())); }
    },
    Tip{
        [&ch = binds::TOGGLE_CHAT, &exch = binds::TOGGLE_EXTENDED_CHAT]() -> std::string {
            if(!BanchoState::is_online()) return "";
            return tformat(R"(Press {:s} or {:s} anywhere to open chat.)", env->scanCodeToString(ch.get()), env->scanCodeToString(exch.get()));
        }
    },
#if !defined(MCENGINE_PLATFORM_WASM) // irrelevant for web
#if defined(MCENGINE_FEATURE_BASS) && defined(MCENGINE_FEATURE_SOLOUD)
#if defined(MCENGINE_PLATFORM_WINDOWS)
	Tip{_(R"(Launch with the neomod-BASS shortcut to use BASS (ASIO/Exclusive mode) for audio.)")},
#else
	Tip{_(R"(Launch with "-sound bass" as a commandline argument to use BASS for audio.)")},
#endif // defined(MCENGINE_PLATFORM_WINDOWS)
#endif // defined(MCENGINE_FEATURE_BASS) && defined(MCENGINE_FEATURE_SOLOUD)
#if defined(MCENGINE_FEATURE_SDLGPU)
#if defined(MCENGINE_PLATFORM_WINDOWS)
    Tip{_(R"(Put "-gpu" after the "Target:" field in a shortcut to neomod to use the D3D12 renderer.)")},
    Tip{_(R"(Put "-gpu vk" after the "Target:" field in a shortcut to neomod to use the Vulkan renderer.)")},
#else
    Tip{_(R"(Launch with "-sdlgpu" as a commandline argument to use the Vulkan renderer.)")},
#endif // defined(MCENGINE_PLATFORM_WINDOWS)
#endif // defined(MCENGINE_FEATURE_SDLGPU)
#if defined(MCENGINE_FEATURE_DIRECTX11)
#if defined(MCENGINE_PLATFORM_WINDOWS)
    Tip{_(R"(Put "-dx11" after the "Target:" field in a shortcut to neomod to use the D3D11 renderer.)")},
#else
    Tip{_(R"(Launch with "-dx11" as a commandline argument to use the D3D11 renderer.)")},
#endif // defined(MCENGINE_PLATFORM_WINDOWS)
#endif // defined(MCENGINE_FEATURE_DIRECTX11)
#endif // !defined(MCENGINE_PLATFORM_WASM)
    Tip{_(R"(Press Ctrl + O to open the options menu from anywhere.)")},
        // clang-format on
    };

    return s_tips;
}

int s_currentIdx{-1};

}  // namespace

WrappedText::WrappedText(McFont *font, float xPos, float yPos, float xSize, float ySize)
    : CBaseUIContainer(xPos, yPos, xSize, ySize), font(font) {}

WrappedText::~WrappedText() = default;

WrappedText *WrappedText::setFont(McFont *font) {
    this->font = font;
    return this;
}

WrappedText *WrappedText::setOnMouseUpInsideCallback(std::function<void(bool, bool)> cb) {
    this->onMouseUpCB = std::move(cb);
    return this;
}

WrappedText *WrappedText::setText(const std::string &text) {
    const float containerWidth = this->getSize().x;
    if(text == this->lastText && containerWidth == this->lastWrapWidth) return this;
    this->lastText = text;
    this->lastWrapWidth = containerWidth;
    this->freeElements();
    const float yPad = std::round((float)this->font->getDPI() / 96.f) + 5.f;
    const float lineHeight = this->font->getHeight() + yPad;
    const auto lines = this->font->wrap(text, containerWidth);
    float yCounter = 0.f;
    for(const auto &line : lines) {
        auto *label = new CBaseUILabel(0, yCounter, containerWidth, lineHeight, "", line);
        label->setTextColor(rgb(200, 200, 200))
            ->setShadowColor(rgb(50, 50, 50))
            ->setTextJustification(TEXT_JUSTIFICATION::CENTERED)
            ->setDrawTextShadow(true)
            ->setDrawBackground(false)
            ->setDrawFrame(false);
        yCounter += lineHeight;
        CBaseUIContainer::addBaseUIElement(label);
    }
    this->setSizeY(yCounter);
    return this;
}

void WrappedText::onMouseUpInside(bool left, bool right) {
    if(this->onMouseUpCB) {
        this->onMouseUpCB(left, right);
    }
}

std::string getCurrentTip() {
    std::string current;
    if(s_currentIdx == -1 || (current = getAllTips()[s_currentIdx].get()).empty()) {
        cycleToNextTip();
    }
    current = getAllTips()[s_currentIdx].get();
    assert(!current.empty());
    return tformat("Tip: {:s}", current);
}

void cycleTip(int addIndex) {
    assert(getAllTips().size() > 0);
    if(addIndex > 0) {
        addIndex = std::min(addIndex, static_cast<int>(getAllTips().size() - 1));
    } else if(addIndex < 0) {
        addIndex = -std::min(-addIndex, static_cast<int>(getAllTips().size() - 1));
    }
    if(addIndex == 0) {
        return;
    }

    bool foundNonEmpty = false;
    for(int iteratedOver = 0; !foundNonEmpty && iteratedOver < getAllTips().size(); iteratedOver += addIndex) {
        const int lastTip = cv::main_menu_last_tip_index.getInt();
        s_currentIdx = (lastTip + addIndex) % static_cast<int>(getAllTips().size());
        cv::main_menu_last_tip_index.setValue(s_currentIdx);
        foundNonEmpty = !getAllTips()[s_currentIdx].get().empty();
    }
    return;
}

void cycleToPreviousTip() {
    cycleTip(-1);
    return;
}

void cycleToNextTip() {
    cycleTip(1);
    return;
}

}  // namespace neomod::mainmenu
