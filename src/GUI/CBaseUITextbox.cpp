// Copyright (c) 2015, PG, All rights reserved.
// TODO: make scrolling anims fps independent

#include "CBaseUITextbox.h"

#include <utility>

#include "App.h"
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
#include "SString.h"
#include "Environment.h"
#include "UniString.h"
#include "crypto.h"

// #include "Logging.h"

namespace {
// where a word jump from `pos` goes: back to the start of the word before it, forward to the start of the next one
int wordJump(std::string_view text, int pos, bool forward) {
    const auto isSpace = [&](uSz from, uSz to) { return SString::is_wspace_only(text.substr(from, to - from)); };
    uSz at = pos;
    if(forward) {
        while(at < text.size() && !isSpace(at, UniString::next(text, at))) at = UniString::next(text, at);
        while(at < text.size() && isSpace(at, UniString::next(text, at))) at = UniString::next(text, at);
    } else {
        while(at > 0 && isSpace(UniString::prev(text, at), at)) at = UniString::prev(text, at);
        while(at > 0 && !isSpace(UniString::prev(text, at), at)) at = UniString::prev(text, at);
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
        std::fill_n(stars.begin(), len, '*');
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
    g->setColor(this->textColor);
    g->pushTransform();
    {
        g->translate((int)(this->getPos().x + this->iTextAddX + this->fTextScrollAddX),
                     (int)(this->getPos().y + this->iTextAddY));
        g->drawString(this->font, this->getVisibleText());
    }
    g->popTransform();
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
                    std::clamp<int>(this->fTextScrollAddX + scrollspeed, 0,
                                    this->fTextWidth - this->getSize().x + this->getTextOffsetX() * 2);
                /// animation->moveSmoothEnd(&m_fTextScrollAddX, clampi(this->fTextScrollAddX+scrollspeed, 0,
                /// m_fTextWidth-m_vSize.x+cv::ui_textbox_text_offset_x.getInt()*2), 1);
            }

            if(mouseX > this->getSize().x * 0.85f) {
                const int scrollspeed = mouseX > this->getSize().x ? std::abs(mouseX - this->getSize().x) / 2 + 1 : 1;

                // TODO: animations which don't suck for usability
                this->fTextScrollAddX =
                    std::clamp<int>(this->fTextScrollAddX - scrollspeed, 0,
                                    this->fTextWidth - this->getSize().x + this->getTextOffsetX() * 2);
                /// animation->moveSmoothEnd(&m_fTextScrollAddX, clampi(this->fTextScrollAddX-scrollspeed, 0,
                /// m_fTextWidth-m_vSize.x+cv::ui_textbox_text_offset_x.getInt()*2), 1);
            }
        }

        // handle selecting end
        this->iSelectEnd = this->hitTestCaret(visible_text, mouseX);
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
                this->moveCaret(std::clamp(std::min(this->iSelectStart, this->iSelectEnd), 0, length), false);
            else
                this->moveCaret((int)UniString::prev(this->text, this->caretPosition), shift);
            break;

        case KEY_RIGHT:
            if(control)
                this->moveCaret(this->is_password ? length : wordJump(this->text, this->caretPosition, true), shift);
            else if(!shift && this->hasSelectedText())
                this->moveCaret(std::clamp(std::max(this->iSelectStart, this->iSelectEnd), 0, length), false);
            else
                this->moveCaret((int)UniString::next(this->text, this->caretPosition), shift);
            break;

        case KEY_C:
            if(control && this->hasSelectedText()) env->setClipBoardText(this->getSelectedText());
            break;

        case KEY_V:
            if(control) this->insertTextFromClipboard();
            break;

        case KEY_A:
            if(control) this->selectAll();
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

void CBaseUITextbox::onChar(KeyboardEvent &e) {
    if(!this->isFocused() || !this->bVisible) return;

    e.consume();

    // ignore any control characters, we only want text
    // funny story: Windows 10 still has this bug even today, where when editing the name of any shortcut/folder on the
    // desktop, hitting CTRL + BACKSPACE will insert an invalid character
    if(e.getCharCode() < 32 || (keyboard->isSuperDown() || (keyboard->isControlDown() && !keyboard->isAltDown())))
        return;

    // Linux inserts a weird character when pressing the delete key
    if(e.getCharCode() == 127) return;

    // delete any potentially selected text
    this->handleDeleteSelectedText();

    // add the pressed letter to the text
    {
        char32_t ch = e.getCharCode();
        auto inserted = UniString::to_utf8(std::u32string_view{&ch, 1});
        this->text.insert(this->caretPosition, inserted);
        this->caretPosition += inserted.length();

        this->setText(this->text);
    }

    this->tickCaret();

    Sound *sounds[] = {app->getSound(ActionSound::TYPING1), app->getSound(ActionSound::TYPING2),
                       app->getSound(ActionSound::TYPING3), app->getSound(ActionSound::TYPING4)};
    soundEngine->play(sounds[prand() % 4]);
}

void CBaseUITextbox::moveCaret(int position, bool select) {
    if(!select) {
        this->deselectText();
    } else if(!this->hasSelectedText()) {
        this->iSelectStart = this->caretPosition;
        this->iSelectX = this->font->getStringWidth(this->getVisibleText().substr(0, this->iSelectStart));
    }
    this->caretPosition = position;
    if(select) this->iSelectEnd = position;

    this->tickCaret();
    this->handleCaretKeyboardMove();
    this->updateCaretX();

    soundEngine->play(app->getSound(ActionSound::MOVE_TEXT_CURSOR));
}

void CBaseUITextbox::handleCaretKeyboardMove() {
    const int caretPosition = this->iTextAddX +
                              this->font->getStringWidth(this->getVisibleText().substr(0, this->caretPosition)) +
                              this->fTextScrollAddX;
    if(caretPosition < 0)
        this->fTextScrollAddX += std::abs(caretPosition) + this->getTextOffsetX();
    else if(caretPosition > (this->getSize().x - this->getTextOffsetX()))
        this->fTextScrollAddX -= std::abs(caretPosition - this->getSize().x) + this->getTextOffsetX();
}

void CBaseUITextbox::handleCaretKeyboardDelete() {
    if(this->fTextWidth > this->getSize().x) {
        const int caretPosition = this->iTextAddX +
                                  this->font->getStringWidth(this->getVisibleText().substr(0, this->caretPosition)) +
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
    this->caretPosition = std::clamp<int>(this->caretPosition, 0, this->text.length());

    // handle text justification
    this->fTextWidth = this->font->getStringWidth(this->getVisibleText());
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
    std::string text = this->getVisibleText().substr(0, this->caretPosition);
    this->iCaretX = this->font->getStringWidth(text);
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

void CBaseUITextbox::insertTextFromClipboard() {
    const std::string_view clipstring = env->getClipBoardText();

    /*
    debugLog("got clip string: {:s}", clipstring.toUtf8());
    for (int i=0; i<clipstring.length(); i++)
    {
            debugLog("char #{:d} = {:d}", i, clipstring[i]);
    }
    */

    if(clipstring.length() > 0) {
        this->handleDeleteSelectedText();
        {
            this->text.insert(this->caretPosition, clipstring);
            this->caretPosition = this->caretPosition + clipstring.length();
        }
        this->setText(this->text);
    }
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

void CBaseUITextbox::onMouseDownInside(bool left, bool right) {
    CBaseUIElement::onMouseDownInside(left, right);

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
        if(left) {
            // begin a fresh selection at the click point; captured moves extend it
            this->iSelectStart = this->iSelectEnd = hit;
            this->iSelectX = this->font->getStringWidth(visibleText.substr(0, this->iSelectStart));
            this->caretPosition = hit;
        } else if(!this->hasSelectedText()) {
            this->caretPosition = hit;
        }
        this->updateCaretX();
    }
}

void CBaseUITextbox::onMouseCancel() { this->bBusy = false; }

void CBaseUITextbox::onMouseUpInside(bool left, bool right) {
    CBaseUIElement::onMouseUpInside(left, right);

    this->bBusy = false;
}

void CBaseUITextbox::onMouseUpOutside(bool left, bool right) {
    CBaseUIElement::onMouseUpOutside(left, right);

    this->bBusy = false;
}
