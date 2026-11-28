// Copyright (c) 2015, PG, All rights reserved.
// TODO: make scrolling anims fps independent

#include "CBaseUITextbox.h"
#include "Algorithms.h"

#include <cmath>
#include <string>
#include <string_view>
#include <utility>

#include "App.h"
#include "BaseEnvironment.h"
#include "ConVar.h"
#include "Cursors.h"
#include "Engine.h"
#include "Keyboard.h"
#include "Logging.h"
#include "Mouse.h"
#include "ResourceManager.h"
#include "SoundEngine.h"
#include "CBaseUIDispatch.h"
#include "Font.h"
#include "Graphics.h"
#include "Environment.h"
#include "UniString.h"
#include "crypto.h"

// #include "Logging.h"

namespace {
// a word (what a double click selects, what Ctrl+Left/Right step over): a run of characters of one kind
enum class CharKind : u8 { SPACE, PUNCTUATION, WORD, HIRAGANA, KATAKANA, HAN };

CharKind kindOf(char32_t c) {
    if(c < 0x80) {
        if(c == ' ' || (c >= '\t' && c <= '\r')) return CharKind::SPACE;
        if((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')
            return CharKind::WORD;
        return CharKind::PUNCTUATION;
    }
    // Japanese has no spaces between words, so its scripts take turns instead
    static constexpr struct {
        char32_t first, last;
        CharKind kind;
    } ranges[]{
        {0x00A0, 0x00A0, CharKind::SPACE},       {0x00A1, 0x00BF, CharKind::PUNCTUATION},
        {0x00D7, 0x00D7, CharKind::PUNCTUATION}, {0x00F7, 0x00F7, CharKind::PUNCTUATION},
        {0x2000, 0x200B, CharKind::SPACE},       {0x2010, 0x205E, CharKind::PUNCTUATION},
        {0x3000, 0x3000, CharKind::SPACE},       {0x3001, 0x3004, CharKind::PUNCTUATION},
        {0x3005, 0x3007, CharKind::HAN},         {0x3008, 0x303F, CharKind::PUNCTUATION},
        {0x3040, 0x309F, CharKind::HIRAGANA},    {0x30A0, 0x30FA, CharKind::KATAKANA},
        {0x30FB, 0x30FB, CharKind::PUNCTUATION}, {0x30FC, 0x30FF, CharKind::KATAKANA},
        {0x3400, 0x4DBF, CharKind::HAN},         {0x4E00, 0x9FFF, CharKind::HAN},
        {0xF900, 0xFAFF, CharKind::HAN},         {0xFF01, 0xFF0F, CharKind::PUNCTUATION},
        {0xFF1A, 0xFF20, CharKind::PUNCTUATION}, {0xFF3B, 0xFF40, CharKind::PUNCTUATION},
        {0xFF5B, 0xFF65, CharKind::PUNCTUATION}, {0xFF66, 0xFF9F, CharKind::KATAKANA},
        {0x20000, 0x3FFFF, CharKind::HAN},
    };
    for(const auto &range : ranges) {
        if(c >= range.first && c <= range.last) return range.kind;
    }
    return CharKind::WORD;
}

// the kind of the character at byte `pos`: a '.' or an apostrophe between two letters belongs to them ("1.5", "don't")
CharKind kindAt(std::string_view text, uSz pos) {
    const auto at = [text](uSz from) { return *UniString::codepoints(text.substr(from)).begin(); };
    const char32_t c = at(pos);
    const uSz next = UniString::next(text, pos);
    if((c == '.' || c == '\'' || c == U'\u2019') && pos > 0 && next < text.size() &&
       kindOf(at(UniString::prev(text, pos))) == CharKind::WORD && kindOf(at(next)) == CharKind::WORD)
        return CharKind::WORD;
    return kindOf(c);
}

// where the run of `kind` that ends at `pos` starts
uSz runStart(std::string_view text, uSz pos, CharKind kind) {
    while(pos > 0 && kindAt(text, UniString::prev(text, pos)) == kind) pos = UniString::prev(text, pos);
    return pos;
}

// where the run of `kind` that starts at `pos` ends
uSz runEnd(std::string_view text, uSz pos, CharKind kind) {
    while(pos < text.size() && kindAt(text, pos) == kind) pos = UniString::next(text, pos);
    return pos;
}

// where a word jump from `pos` goes: back to the start of the word before it, forward to the start of the next one
int wordJump(std::string_view text, int pos, bool forward) {
    uSz at = pos;
    if(forward) {
        if(at < text.size()) at = runEnd(text, at, kindAt(text, at));
        at = runEnd(text, at, CharKind::SPACE);
    } else {
        at = runStart(text, at, CharKind::SPACE);
        if(at > 0) at = runStart(text, at, kindAt(text, UniString::prev(text, at)));
    }
    return (int)at;
}
}  // namespace

CBaseUITextbox::CBaseUITextbox(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    this->font = engine->getDefaultFont();

    this->textColor = this->frameColor = this->caretColor = 0xffffffff;
    this->backgroundColor = 0xff000000;
    this->frameBrightColor = 0;
    this->frameDarkColor = 0;

    this->bDrawFrame = true;
    this->bDrawBackground = true;

    // a middle click pastes the primary selection, where the system has one
    this->bHandleMiddleMouse = Env::cfg(OS::LINUX);

    this->iTextAddX = this->getTextOffsetX();
    this->iTextAddY = 0;
    this->fTextScrollAddX = 0;
    this->iSelectX = 0;
    this->iCaretX = 0;
    this->iCaretWidth = 2;

    this->bHitenter = false;
    this->iSelectStart = 0;
    this->iSelectEnd = 0;

    this->fTextWidth = 0.0f;
    this->caretPosition = 0;
}

std::string CBaseUITextbox::getVisibleText() {
    if(this->is_password) {
        const uSz len = UniString::num_codepoints(this->text);
        std::string stars;
        stars.resize(len);
        Mc::fill_n(stars.begin(), len, '*');
        return stars;
    } else {
        return this->text;
    }
}

void CBaseUITextbox::draw() {
    if(!this->bVisible) return;

    const float dpiScale = ((float)this->font->getDPI() / 96.0f);  // NOTE: abusing font dpi

    // draw background
    if(this->bDrawBackground) {
        g->setColor(this->backgroundColor);
        g->fillRect(this->getPos(), this->getSize());
    }

    // draw base frame
    if(this->bDrawFrame) {
        if(this->frameDarkColor != 0 || this->frameBrightColor != 0)
            g->drawRect(this->getPos(), this->getSize(), this->frameDarkColor, this->frameBrightColor,
                        this->frameBrightColor, this->frameDarkColor);
        else {
            g->setColor(this->frameColor);
            g->drawRect(this->getPos(), this->getSize());
        }
    }

    // draw text

    if(this->font == nullptr) return;

    g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 1, this->getSize().x - 1, this->getSize().y));
    {
        // draw selection box
        if(this->hasSelectedText()) {
            const int xpos1 = this->getPos().x + this->iTextAddX + this->iCaretX + this->fTextScrollAddX;
            const int xpos2 = this->getPos().x + this->iSelectX + this->iTextAddX + this->fTextScrollAddX;

            g->setColor(this->selectionColor);
            if(xpos1 > xpos2)
                g->fillRect(xpos2, this->getPos().y + 1, xpos1 - xpos2 + 2, this->getSize().y - 1);
            else
                g->fillRect(xpos1, this->getPos().y + 1, xpos2 - xpos1, this->getSize().y - 1);
        }

        this->drawText();

        // draw caret
        if(this->isFocused()) {
            const int caretWidth = std::round((float)this->iCaretWidth * dpiScale);
            const int height = std::round(this->getSize().y - 2 * 3 * dpiScale);
            const float yadd = std::round(height / 2.0f);

            g->setColor(this->caretColor);
            g->fillRect((int)(this->getPos().x + this->iTextAddX + this->iCaretX + this->fTextScrollAddX),
                        (int)(this->getPos().y + this->getSize().y / 2.0f - yadd), caretWidth, height);
        }
    }
    g->popClipRect();
}

void CBaseUITextbox::drawText() {
    const DrawnText drawn = this->getDrawnText();
    const int x = (int)(this->getPos().x + this->iTextAddX + this->fTextScrollAddX);
    const int y = (int)(this->getPos().y + this->iTextAddY);

    g->setColor(this->textColor);
    g->pushTransform();
    {
        g->translate(x, y);
        g->drawString(this->font, drawn.text);
    }
    g->popTransform();

    // what the IME composes is underlined, the part it converts thicker
    if(drawn.compositionEnd > drawn.compositionStart) {
        const auto at = [&](int pos) {
            return x + (int)this->font->getStringWidth(std::string_view{drawn.text}.substr(0, pos));
        };
        const float dpiScale = (float)this->font->getDPI() / 96.0f;
        const int thickness = Mc::max(1, (int)std::round(dpiScale));
        const int lineY = y + (int)std::round(2 * dpiScale);
        g->fillRect(at(drawn.compositionStart), lineY, at(drawn.compositionEnd) - at(drawn.compositionStart),
                    thickness);
        if(drawn.clauseEnd > drawn.clauseStart)
            g->fillRect(at(drawn.clauseStart), lineY, at(drawn.clauseEnd) - at(drawn.clauseStart), thickness * 2);
    }
}

CBaseUITextbox::DrawnText CBaseUITextbox::getDrawnText() {
    std::string drawn = this->getVisibleText();
    const int caret = Mc::clamp(this->caretPosition, 0, (int)drawn.size());
    const TextComposition *composition = keyboard->getComposition(this);
    if(composition == nullptr) return {std::move(drawn), caret, caret, caret, caret, caret};

    // a masked composition keeps its caret at the end
    const std::string composed =
        this->is_password ? std::string(UniString::num_codepoints(composition->text), '*') : composition->text;
    const int cursor = this->is_password ? (int)composed.size() : composition->cursor;
    const int clause = this->is_password ? 0 : composition->clauseLength;
    drawn.insert(caret, composed);
    return {.text = std::move(drawn),
            .caret = caret + cursor + clause,
            .compositionStart = caret,
            .compositionEnd = caret + (int)composed.size(),
            .clauseStart = caret + cursor,
            .clauseEnd = caret + cursor + clause};
}

void CBaseUITextbox::tick() {
    CBaseUIElement::tick();
    if(!this->bVisible) return;

    // update caret blinking
    {
        if(!this->isFocused()) {
            this->tickCaret();
        } else {
            const double elapsed = engine->getTime() - this->fCaretBlinkStartTime;
            const float newAbsAlpha =
                static_cast<float>((std::cos((elapsed / cv::ui_textbox_caret_blink_time.getDouble()) * PI) + 1.) / 2.);
            this->caretColor.setA(newAbsAlpha);
        }
    }
}

void CBaseUITextbox::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIElement::updateInput(c);

    // HACKHACK: should do this with the proper events! this will only work properly though if we can event.consume()
    // charDown's
    if(!this->bEnabled && this->isFocused() && mouse->isLeftDown() && !this->isMouseInside()) this->stealFocus();
}

std::optional<CURSORTYPE> CBaseUITextbox::getCursorShape() { return CURSORTYPE::CURSOR_TEXT; }

// find the codepoint boundary closest to the mouse position
int CBaseUITextbox::hitTestCaret(std::string_view vt, int mx) const {
    // mx is relative to the box, the text starts iTextAddX + fTextScrollAddX into it
    return static_cast<int>(this->font->hitTest(vt, static_cast<float>(mx - this->iTextAddX) - this->fTextScrollAddX));
}

std::pair<int, int> CBaseUITextbox::hitTestWord(std::string_view vt, int mx) const {
    // the character under the cursor (past the end of the text: the last one)
    auto at = static_cast<uSz>(this->hitTestCaret(vt, mx));
    if(at == vt.size() || (at > 0 && static_cast<float>(mx - this->iTextAddX) - this->fTextScrollAddX <
                                         this->font->getStringWidth(vt.substr(0, at))))
        at = UniString::prev(vt, at);

    const CharKind kind = kindAt(vt, at);
    return {static_cast<int>(runStart(vt, at, kind)), static_cast<int>(runEnd(vt, at, kind))};
}

void CBaseUITextbox::onCapturedMouseMove() {
    // selecting and scrolling: the selection was begun at the down, captured moves extend it
    const MouseButtonFlags held = CBaseUIDispatch::getCaptorButtons();
    const bool mleft = flags::has<MouseButtonFlags::MF_LEFT>(held);
    const bool mright = flags::has<MouseButtonFlags::MF_RIGHT>(held);
    if((!mleft && !mright) || this->text.length() == 0) return;

    this->tickCaret();

    const int mouseX = mouse->getPos().x - this->getPos().x;
    auto visible_text = this->getVisibleText();

    // handle scrolling
    if(mleft) {
        if(this->fTextWidth > this->getSize().x) {
            if(mouseX < this->getSize().x * 0.15f) {
                const int scrollspeed = mouseX < 0 ? std::abs(mouseX) / 2 + 1 : 3;

                // TODO: animations which don't suck for usability
                this->fTextScrollAddX =
                    Mc::clamp<int>(this->fTextScrollAddX + scrollspeed, 0,
                                   this->fTextWidth - this->getSize().x + this->getTextOffsetX() * 2);
                /// animation->moveSmoothEnd(&m_fTextScrollAddX, clampi(this->fTextScrollAddX+scrollspeed, 0,
                /// m_fTextWidth-m_vSize.x+cv::ui_textbox_text_offset_x.getInt()*2), 1);
            }

            if(mouseX > this->getSize().x * 0.85f) {
                const int scrollspeed = mouseX > this->getSize().x ? std::abs(mouseX - this->getSize().x) / 2 + 1 : 1;

                // TODO: animations which don't suck for usability
                this->fTextScrollAddX =
                    Mc::clamp<int>(this->fTextScrollAddX - scrollspeed, 0,
                                   this->fTextWidth - this->getSize().x + this->getTextOffsetX() * 2);
                /// animation->moveSmoothEnd(&m_fTextScrollAddX, clampi(this->fTextScrollAddX-scrollspeed, 0,
                /// m_fTextWidth-m_vSize.x+cv::ui_textbox_text_offset_x.getInt()*2), 1);
            }
        }

        // handle selecting end (after a triple click everything stays selected)
        if(this->iSelectClicks == 1) {
            this->iSelectEnd = this->hitTestCaret(visible_text, mouseX);
        } else if(this->iSelectClicks == 2) {
            // the word the selection began on and those up to the cursor, with the caret on the far side
            // (the text can have changed under a held button)
            const auto [start, end] = this->hitTestWord(visible_text, mouseX);
            const int size = static_cast<int>(visible_text.size());
            const bool back = start < this->iWordStart;
            this->iSelectStart = Mc::min(back ? this->iWordEnd : this->iWordStart, size);
            this->iSelectEnd = back ? start : Mc::min(Mc::max(end, this->iWordEnd), size);
            this->iSelectX = this->font->getStringWidth(visible_text.substr(0, this->iSelectStart));
        }
        this->caretPosition = this->iSelectEnd;
    } else {
        if(!this->hasSelectedText()) {
            this->caretPosition = this->hitTestCaret(visible_text, mouseX);
        }
    }

    // update caretx
    this->updateCaretX();
}

bool CBaseUITextbox::isActive() { return CBaseUIElement::isActive() || this->isFocused(); }

void CBaseUITextbox::onFocusStolen() {
    // what was being composed stays here, as in native fields
    keyboard->commitComposition(this);
    this->bBusy = false;
    this->deselectText();
}

void CBaseUITextbox::onKeyDown(KeyboardEvent &e) {
    if(!this->isFocused() || !this->bVisible) return;

    e.consume();

    const bool control = e.getModifiers() & KEYMOD_CONTROL;
    const bool shift = e.getModifiers() & KEYMOD_SHIFT;
    const int length = (int)this->text.length();

    switch(e.getScanCode()) {
        case KEY_DELETE:
            soundEngine->play(app->getSound(ActionSound::DELETING_TEXT));
            if(this->text.length() > 0) {
                if(this->hasSelectedText())
                    this->handleDeleteSelectedText();
                else if(this->caretPosition < this->text.length()) {
                    const uSz next = UniString::next(this->text, this->caretPosition);
                    this->text.erase(this->caretPosition, next - this->caretPosition);

                    this->setText(this->text);
                }
            }
            this->tickCaret();
            break;

        case KEY_ENTER:
        case KEY_NUMPAD_ENTER:
            this->bHitenter = true;
            break;

        case KEY_ESCAPE:
            this->stealFocus();
            break;

        case KEY_BACKSPACE:
            soundEngine->play(app->getSound(ActionSound::DELETING_TEXT));
            if(this->text.length() > 0) {
                if(this->hasSelectedText())
                    this->handleDeleteSelectedText();
                else if(this->caretPosition - 1 >= 0) {
                    if(control && this->is_password) this->setText("");

                    // with control the word before the caret, with the spaces between them
                    const int from = control ? wordJump(this->text, this->caretPosition, false)
                                             : (int)UniString::prev(this->text, this->caretPosition);
                    if(from < this->caretPosition) {
                        this->text.erase(from, this->caretPosition - from);
                        this->caretPosition = from;
                    }

                    this->setText(this->text);
                }
            }
            this->tickCaret();
            break;

        // with shift the selection goes along, with control the caret jumps by words (a password's to its ends)
        case KEY_LEFT:
            if(control)
                this->moveCaret(this->is_password ? 0 : wordJump(this->text, this->caretPosition, false), shift);
            else if(!shift && this->hasSelectedText())
                this->moveCaret(Mc::clamp(Mc::min(this->iSelectStart, this->iSelectEnd), 0, length), false);
            else
                this->moveCaret((int)UniString::prev(this->text, this->caretPosition), shift);
            break;

        case KEY_RIGHT:
            if(control)
                this->moveCaret(this->is_password ? length : wordJump(this->text, this->caretPosition, true), shift);
            else if(!shift && this->hasSelectedText())
                this->moveCaret(Mc::clamp(Mc::max(this->iSelectStart, this->iSelectEnd), 0, length), false);
            else
                this->moveCaret((int)UniString::next(this->text, this->caretPosition), shift);
            break;

        case KEY_C:
            if(control && this->hasSelectedText()) env->setClipBoardText(this->getSelectedText());
            break;

        case KEY_V:
            if(control) this->insertText(env->getClipBoardText());
            break;

        case KEY_A:
            if(control) {
                this->selectAll();
                this->publishSelection();
            }
            break;

        case KEY_X:
            if(control && this->hasSelectedText()) {
                soundEngine->play(app->getSound(ActionSound::DELETING_TEXT));
                env->setClipBoardText(this->getSelectedText());
                this->handleDeleteSelectedText();
            }
            break;

        case KEY_HOME:
            this->moveCaret(0, shift);
            break;

        case KEY_END:
            this->moveCaret(length, shift);
            break;
        default:
            break;
    }
}

void CBaseUITextbox::onTextInput(std::string_view text) {
    // typing replaces the selection
    this->handleDeleteSelectedText();
    this->text.insert(this->caretPosition, text);
    this->caretPosition += (int)text.size();
    this->setText(this->text);

    this->tickCaret();

    Sound *sounds[] = {app->getSound(ActionSound::TYPING1), app->getSound(ActionSound::TYPING2),
                       app->getSound(ActionSound::TYPING3), app->getSound(ActionSound::TYPING4)};
    soundEngine->play(sounds[prand() % 4]);
}

void CBaseUITextbox::onTextComposition(const TextComposition &composition) {
    // so does composing
    if(!composition.text.empty()) this->handleDeleteSelectedText();
    this->setText(this->text);
    this->tickCaret();
}

TextInputArea CBaseUITextbox::getTextInputArea() {
    const float left = this->getPos().x;
    const float right = left + this->getSize().x;
    const float textX = left + (float)this->iTextAddX + this->fTextScrollAddX;
    const float caret = Mc::clamp(textX + (float)this->iCaretX, left, right);

    // from where the composition starts, where the candidates go below on every platform
    float start = caret;
    if(keyboard->getComposition(this) != nullptr) {
        const DrawnText drawn = this->getDrawnText();
        const float width = this->font->getStringWidth(std::string_view{drawn.text}.substr(0, drawn.compositionStart));
        start = Mc::clamp(textX + width, left, right);
    }
    return {.rect = McRect{start, this->getPos().y, right - start, this->getSize().y}, .caretX = caret};
}

TextInputType CBaseUITextbox::getTextInputType() const {
    return this->is_password ? TextInputType::PASSWORD : TextInputType::TEXT;
}

void CBaseUITextbox::moveCaret(int position, bool select) {
    if(!select) {
        this->deselectText();
    } else if(!this->hasSelectedText()) {
        this->iSelectStart = this->caretPosition;
        this->iSelectX = this->font->getStringWidth(this->getVisibleText().substr(0, this->iSelectStart));
    }
    this->caretPosition = position;
    if(select) {
        this->iSelectEnd = position;
        this->publishSelection();
    }

    this->tickCaret();
    this->handleCaretKeyboardMove();
    this->updateCaretX();

    soundEngine->play(app->getSound(ActionSound::MOVE_TEXT_CURSOR));
}

void CBaseUITextbox::handleCaretKeyboardMove() {
    const DrawnText drawn = this->getDrawnText();
    const int caretPosition = this->iTextAddX +
                              this->font->getStringWidth(std::string_view{drawn.text}.substr(0, drawn.caret)) +
                              this->fTextScrollAddX;
    if(caretPosition < 0)
        this->fTextScrollAddX += std::abs(caretPosition) + this->getTextOffsetX();
    else if(caretPosition > (this->getSize().x - this->getTextOffsetX()))
        this->fTextScrollAddX -= std::abs(caretPosition - this->getSize().x) + this->getTextOffsetX();
}

void CBaseUITextbox::handleCaretKeyboardDelete() {
    if(this->fTextWidth > this->getSize().x) {
        const DrawnText drawn = this->getDrawnText();
        const int caretPosition = this->iTextAddX +
                                  this->font->getStringWidth(std::string_view{drawn.text}.substr(0, drawn.caret)) +
                                  this->fTextScrollAddX;
        if(caretPosition < (this->getSize().x - this->getTextOffsetX()))
            this->fTextScrollAddX += std::abs(this->getSize().x - this->getTextOffsetX() - caretPosition);
    }
}

void CBaseUITextbox::tickCaret() {
    this->caretColor.setA(1.f);
    this->fCaretBlinkStartTime = engine->getTime();
}

bool CBaseUITextbox::hitEnter() {
    if(this->bHitenter) {
        this->bHitenter = false;
        return true;
    } else
        return false;
}

bool CBaseUITextbox::hasSelectedText() const { return ((this->iSelectStart - this->iSelectEnd) != 0); }

void CBaseUITextbox::selectAll() {
    this->iSelectStart = 0;
    this->iSelectX = 0;
    this->caretPosition = this->iSelectEnd = this->text.length();

    this->handleCaretKeyboardMove();
    this->updateCaretX();
}

void CBaseUITextbox::clear() {
    this->deselectText();
    this->setText("");
}

void CBaseUITextbox::focus(bool move_caret) {
    this->requestFocus();
    this->bBusy = true;
    this->bMouseInside = true;

    if(move_caret) {
        this->setCursorPosRight();
    }
}

CBaseUITextbox *CBaseUITextbox::setFont(McFont *font) {
    this->font = font;
    this->setText(this->text);

    return this;
}

CBaseUITextbox *CBaseUITextbox::setText(std::string text) {
    this->text = std::move(text);
    this->caretPosition = Mc::clamp<int>(this->caretPosition, 0, this->text.length());

    // handle text justification
    this->fTextWidth = this->font->getStringWidth(this->getDrawnText().text);
    switch(this->textJustification) {
        case TEXT_JUSTIFICATION::LEFT:
            this->iTextAddX = this->getTextOffsetX();
            break;

        case TEXT_JUSTIFICATION::CENTERED:
            this->iTextAddX = -(this->fTextWidth - this->getSize().x) / 2.0f;
            this->iTextAddX = this->iTextAddX > 0 ? this->iTextAddX : this->getTextOffsetX();
            break;

        case TEXT_JUSTIFICATION::RIGHT:
            this->iTextAddX = (this->getSize().x - this->fTextWidth) - this->getTextOffsetX();
            this->iTextAddX = this->iTextAddX > 0 ? this->iTextAddX : this->getTextOffsetX();
            break;
    }

    // handle over-text
    if(this->fTextWidth > this->getSize().x) {
        this->iTextAddX -= this->fTextWidth - this->getSize().x + this->getTextOffsetX() * 2;
        this->handleCaretKeyboardMove();
    } else
        this->fTextScrollAddX = 0;

    // TODO: force stop animation, it will fuck shit up
    /// animation->moveSmoothEnd(&m_fTextScrollAddX, this->fTextScrollAddX, 0.1f);

    // center vertically
    const float addY = std::round(this->getSize().y / 2.0f + this->font->getHeight() / 2.0f);
    this->iTextAddY = addY > 0 ? addY : 0;

    this->updateCaretX();
    this->updateTextPos();

    return this;
}

void CBaseUITextbox::setCursorPosRight() {
    this->caretPosition = this->text.length();
    {
        this->updateCaretX();
        this->tickCaret();
    }
    this->fTextScrollAddX = 0;
}

void CBaseUITextbox::updateCaretX() {
    const DrawnText drawn = this->getDrawnText();
    this->iCaretX = this->font->getStringWidth(std::string_view{drawn.text}.substr(0, drawn.caret));
}

int CBaseUITextbox::getTextOffsetX() const {
    return this->iTextOffsetX >= 0 ? this->iTextOffsetX : cv::ui_textbox_text_offset_x.getInt();
}

void CBaseUITextbox::handleDeleteSelectedText() {
    if(!this->hasSelectedText()) return;

    const int selectedTextLength = (this->iSelectStart < this->iSelectEnd ? this->iSelectEnd - this->iSelectStart
                                                                          : this->iSelectStart - this->iSelectEnd);
    this->text.erase(this->iSelectStart < this->iSelectEnd ? this->iSelectStart : this->iSelectEnd, selectedTextLength);

    if(this->iSelectEnd > this->iSelectStart) this->caretPosition -= selectedTextLength;

    this->deselectText();

    this->setText(this->text);
}

void CBaseUITextbox::insertText(std::string_view inserted) {
    if(inserted.length() > 0) {
        this->handleDeleteSelectedText();
        {
            this->text.insert(this->caretPosition, inserted);
            this->caretPosition = this->caretPosition + inserted.length();
        }
        this->setText(this->text);
    }
}

void CBaseUITextbox::publishSelection() {
    if(!this->is_password && this->hasSelectedText()) env->setPrimarySelectionText(this->getSelectedText());
}

void CBaseUITextbox::updateTextPos() {
    if(this->textJustification == TEXT_JUSTIFICATION::LEFT) {
        if((this->iTextAddX + this->fTextScrollAddX) > this->getTextOffsetX()) {
            if(this->hasSelectedText() && this->caretPosition == 0) {
                this->fTextScrollAddX = this->getTextOffsetX() - this->iTextAddX;
            } else
                this->fTextScrollAddX = this->getTextOffsetX() - this->iTextAddX;
        }
    }
}

void CBaseUITextbox::deselectText() {
    this->iSelectStart = 0;
    this->iSelectEnd = 0;
}

std::string CBaseUITextbox::getSelectedText() {
    const int selectedTextLength = (this->iSelectStart < this->iSelectEnd ? this->iSelectEnd - this->iSelectStart
                                                                          : this->iSelectStart - this->iSelectEnd);

    if(selectedTextLength > 0)
        return this->text.substr(this->iSelectStart < this->iSelectEnd ? this->iSelectStart : this->iSelectEnd,
                                 selectedTextLength);
    else
        return "";
}

void CBaseUITextbox::onResized() {
    CBaseUIElement::onResized();

    // HACKHACK: brute force fix layout
    this->setText(this->text);
}

void CBaseUITextbox::onMouseDownInside(MouseButtonFlags buttons) {
    CBaseUIElement::onMouseDownInside(buttons);

    // the caret moves: what was being composed stays where it was
    keyboard->commitComposition(this);

    // take keyboard focus: relinquishes any other focused textbox (in either root); the
    // dispatch sets bActive on us after this handler returns
    this->requestFocus();

    // force busy + lock: a drag on a textbox is a text selection, an enclosing scrollview must
    // not steal it into a drag-scroll (textbox requires full focus due to text selection)
    this->bBusy = true;
    this->lockCapture();

    this->tickCaret();

    if(this->text.length() > 0) {
        const int mouseX = mouse->getPos().x - this->getPos().x;
        const auto visibleText = this->getVisibleText();
        const int hit = this->hitTestCaret(visibleText, mouseX);
        if(flags::has<MouseButtonFlags::MF_LEFT>(buttons)) {
            // begin a fresh selection: at the click point, the word under it on a double click, everything on a triple
            // click (a password has no words to show); captured moves extend it
            const int clicks = Mc::min<int>(CBaseUIDispatch::getClicks(), 3);
            this->iSelectClicks = this->is_password && clicks == 2 ? 3 : clicks;
            if(this->iSelectClicks == 3) {
                this->selectAll();
            } else if(this->iSelectClicks == 2) {
                const auto [start, end] = this->hitTestWord(visibleText, mouseX);
                this->iWordStart = this->iSelectStart = start;
                this->iWordEnd = this->iSelectEnd = this->caretPosition = end;
            } else {
                this->iSelectStart = this->iSelectEnd = this->caretPosition = hit;
            }
            this->iSelectX = this->font->getStringWidth(visibleText.substr(0, this->iSelectStart));
        } else if(flags::has<MouseButtonFlags::MF_MIDDLE>(buttons)) {
            this->deselectText();
            this->caretPosition = hit;
        } else if(!this->hasSelectedText()) {
            this->caretPosition = hit;
        }
        this->updateCaretX();
    }

    if(flags::has<MouseButtonFlags::MF_MIDDLE>(buttons)) this->insertText(env->getPrimarySelectionText());
}

void CBaseUITextbox::onMouseCancel() { this->bBusy = false; }

void CBaseUITextbox::onMouseUpInside(MouseButtonFlags buttons) {
    CBaseUIElement::onMouseUpInside(buttons);

    this->bBusy = false;
    if(flags::has<MouseButtonFlags::MF_LEFT>(buttons)) this->publishSelection();
}

void CBaseUITextbox::onMouseUpOutside(MouseButtonFlags buttons) {
    CBaseUIElement::onMouseUpOutside(buttons);

    this->bBusy = false;
    if(flags::has<MouseButtonFlags::MF_LEFT>(buttons)) this->publishSelection();
}
