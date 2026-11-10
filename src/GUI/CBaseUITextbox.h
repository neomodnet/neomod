#pragma once
// Copyright (c) 2015, PG, All rights reserved.
#include "CBaseUIElement.h"
#include "Color.h"

#include <utility>

class McFont;

// TODO: so much duplication for text things between textbox/label/button
class CBaseUITextbox : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUITextbox)
   public:
    CBaseUITextbox(float xPos = 0.0f, float yPos = 0.0f, float xSize = 0.0f, float ySize = 0.0f, std::string name = {});
    ~CBaseUITextbox() override = default;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;
    void onFocusStolen() override;
    void onResized() override;
    [[nodiscard]] std::optional<CURSORTYPE> getCursorShape() override;

    // a textbox is "active" while it holds the keyboard focus, on top of the base pressed/busy
    // states (focus is the dispatcher pointer now; bActive is only the transient press)
    bool isActive() override;

    void onChar(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;

    std::string getVisibleText();
    [[nodiscard]] inline std::string_view getText() const { return this->text; }
    [[nodiscard]] inline McFont *getFont() const { return this->font; }

    CBaseUITextbox *setDrawFrame(bool drawFrame) {
        this->bDrawFrame = drawFrame;
        return this;
    }
    CBaseUITextbox *setDrawBackground(bool drawBackground) {
        this->bDrawBackground = drawBackground;
        return this;
    }

    CBaseUITextbox *setBackgroundColor(Color backgroundColor) {
        this->backgroundColor = backgroundColor;
        return this;
    }
    CBaseUITextbox *setTextColor(Color textColor) {
        this->textColor = textColor;
        return this;
    }
    CBaseUITextbox *setCaretColor(Color caretColor) {
        this->caretColor = caretColor;
        return this;
    }
    CBaseUITextbox *setFrameColor(Color frameColor) {
        this->frameColor = frameColor;
        return this;
    }
    CBaseUITextbox *setFrameBrightColor(Color frameBrightColor) {
        this->frameBrightColor = frameBrightColor;
        return this;
    }
    CBaseUITextbox *setFrameDarkColor(Color frameDarkColor) {
        this->frameDarkColor = frameDarkColor;
        return this;
    }

    CBaseUITextbox *setFont(McFont *font);
    CBaseUITextbox *setTextAddX(float textAddX) {
        this->iTextAddX = textAddX;
        return this;
    }
    CBaseUITextbox *setCaretWidth(int caretWidth) {
        this->iCaretWidth = caretWidth;
        return this;
    }
    CBaseUITextbox *setTextJustification(TEXT_JUSTIFICATION textJustification) {
        this->textJustification = textJustification;
        this->setText(this->text);
        return this;
    }

    virtual CBaseUITextbox *setText(std::string text);

    void setCursorPosRight();

    bool hitEnter();
    [[nodiscard]] bool hasSelectedText() const;
    void selectAll();
    void clear();
    void focus(bool move_caret = true);

    // TODO: these should not just be modifiable by anyone
    bool is_password = false;

    std::string text;
    int caretPosition;
    void tickCaret();
    void updateTextPos();

    // Dummy function for RoomScreen generic macro
    void setSizeToContent(int /*a*/, int /*b*/) {}

   protected:
    virtual void drawText();

    // events
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;
    void onMouseCancel() override;
    void onCapturedMouseMove() override;

    [[nodiscard]] int hitTestCaret(std::string_view vt, int mx) const;
    // the word under the cursor as byte offsets [start, end): a run of spaces, punctuation or letters of one script
    [[nodiscard]] std::pair<int, int> hitTestWord(std::string_view vt, int mx) const;

    // puts the caret at a byte position of the text; `select` extends the selection to there (starting one at the caret)
    void moveCaret(int position, bool select);
    void handleCaretKeyboardMove();
    void handleCaretKeyboardDelete();
    void updateCaretX();

    [[nodiscard]] int getTextOffsetX() const;

    void handleDeleteSelectedText();
    void insertTextFromClipboard();
    void deselectText();
    std::string getSelectedText();

    McFont *font;

    double fCaretBlinkStartTime{0.};
    float fTextScrollAddX;
    float fTextWidth;

    int iTextAddX;
    int iTextAddY;
    int iCaretX;
    int iCaretWidth;

    int iSelectStart;
    int iSelectEnd;
    int iSelectX;
    // the presses in a row that began the selection: it extends by characters (1), by words (2) or is everything (3)
    int iSelectClicks{1};
    // the word a selection by words began on
    int iWordStart{0};
    int iWordEnd{0};

    Color textColor;
    Color frameColor;
    Color frameBrightColor;
    Color frameDarkColor;
    Color caretColor;
    Color backgroundColor;
    Color selectionColor{0xff56bcff};

    // between the left edge and the text (negative: ui_textbox_text_offset_x)
    int iTextOffsetX{-1};

    TEXT_JUSTIFICATION textJustification{TEXT_JUSTIFICATION::LEFT};

    bool bHitenter;
    bool bDrawFrame;
    bool bDrawBackground;
};
