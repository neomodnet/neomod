#pragma once
// Copyright (c) 2013, PG, All rights reserved.
#include "BaseEnvironment.h"

#include "CBaseUIElement.h"
#include "Color.h"
#include "MakeDelegateWrapper.h"

#include <utility>
#include <memory>

class McFont;

// TODO: why does this have to basically duplicate all of CBaseUILabel
class CBaseUIButton : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIButton)
   public:
    CBaseUIButton(std::nullptr_t notext, float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0);
    CBaseUIButton(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {},
                  std::string text = {});
    ~CBaseUIButton() override;

    void draw() override;

    void click(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT);

    using CBaseUIButtonClickCB = std::function<void(CBaseUIButton *, MouseButtonFlags)>;

    template <typename Callable>
    CBaseUIButton *setClickCallback(Callable &&cb) {
        using CBType = std::decay_t<Callable>;

        if constexpr(std::is_invocable_v<CBType, CBaseUIButton *, MouseButtonFlags>) {
            this->clickCallback = std::make_unique<CBaseUIButtonClickCB>(std::forward<Callable>(cb));
        } else if constexpr(std::is_invocable_v<CBType, MouseButtonFlags>) {
            this->clickCallback = std::make_unique<CBaseUIButtonClickCB>(
                [cb = std::forward<Callable>(cb)](CBaseUIButton *, MouseButtonFlags buttons) { cb(buttons); });
        } else if constexpr(std::is_invocable_v<CBType, CBaseUIButton *>) {
            this->clickCallback = std::make_unique<CBaseUIButtonClickCB>(
                [cb = std::forward<Callable>(cb)](CBaseUIButton *btn, MouseButtonFlags) { cb(btn); });
        } else if constexpr(std::is_invocable_v<CBType>) {
            this->clickCallback = std::make_unique<CBaseUIButtonClickCB>(
                [cb = std::forward<Callable>(cb)](CBaseUIButton *, MouseButtonFlags) { cb(); });
        } else if constexpr(SA::is_delegate_v<CBType>) {
            using traits = SA::delegate_traits<CBType>;
            using FirstArg = typename traits::template nth_arg<0>;
            static_assert(
                std::is_pointer_v<FirstArg> && std::is_base_of_v<CBaseUIButton, std::remove_pointer_t<FirstArg>>,
                "Delegate first argument must be a pointer to CBaseUIButton or derived");

            if constexpr(traits::arity == 1) {
                this->clickCallback = std::make_unique<CBaseUIButtonClickCB>(
                    [cb = std::forward<Callable>(cb)](CBaseUIButton *btn, MouseButtonFlags) {
                        cb(static_cast<FirstArg>(btn));
                    });
            } else if constexpr(traits::arity == 2) {
                this->clickCallback = std::make_unique<CBaseUIButtonClickCB>(
                    [cb = std::forward<Callable>(cb)](CBaseUIButton *btn, MouseButtonFlags buttons) {
                        cb(static_cast<FirstArg>(btn), buttons);
                    });
            } else {
                static_assert(Env::always_false_v<Callable>, "Unsupported delegate arity for derived button callback");
            }
        } else {
            static_assert(Env::always_false_v<Callable>, "Programmer Error (bad callback signature)");
        }

        return this;
    }

    // set
    CBaseUIButton *setDrawFrame(bool drawFrame);
    CBaseUIButton *setDrawBackground(bool drawBackground);
    CBaseUIButton *setDrawShadow(bool enabled);

    CBaseUIButton *setFrameColor(Color frameColor);
    CBaseUIButton *setBackgroundColor(Color backgroundColor);
    CBaseUIButton *setTextColor(Color textColor);
    CBaseUIButton *setTextBrightColor(Color textBrightColor);
    CBaseUIButton *setTextDarkColor(Color textDarkColor);
    CBaseUIButton *setTextJustification(TEXT_JUSTIFICATION j);

    CBaseUIButton *setText(std::string text);

    CBaseUIButton *setFont(McFont *font);

    virtual CBaseUIButton *setSizeToContent(int horizontalBorderSize = 1, int verticalBorderSize = 1);
    CBaseUIButton *setWidthToContent(int horizontalBorderSize = 1);

    // get
    [[nodiscard]] Color getFrameColor() const;
    [[nodiscard]] Color getBackgroundColor() const;
    [[nodiscard]] Color getTextColor() const;
    [[nodiscard]] std::string_view getText() const;
    [[nodiscard]] McFont *getFont() const;

    // events
    void onMouseUpInside(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override;
    void onResized() override;

   protected:
    virtual void onClicked(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT);

    virtual void drawBackground();
    virtual void drawFrame();
    virtual void drawHoverRect(int distance, bool isClickHeld);
    virtual void drawText();

    void updateStringMetrics();

    std::string sText;

    // callbacks, either void, with ourself as the argument, or with the clicked buttons
    std::unique_ptr<CBaseUIButtonClickCB> clickCallback{nullptr};

    McFont *font;

    float fStringWidth{0.f};
    float fStringHeight{0.f};

    Color frameColor{argb(255, 255, 255, 255)};
    Color backgroundColor{argb(255, 0, 0, 0)};
    Color textColor{argb(255, 255, 255, 255)};
    Color textBrightColor{argb(0, 0, 0, 0)};
    Color textDarkColor{argb(0, 0, 0, 0)};

    // settings
    TEXT_JUSTIFICATION textJustification{TEXT_JUSTIFICATION::CENTERED};

    bool bDrawFrame{true};
    bool bDrawBackground{true};
    bool bDrawShadow{true};
};
