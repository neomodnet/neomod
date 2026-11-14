#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "types.h"

class ConVar;

using SCANCODE = u16;
using KEYCODE = u32;
using KEYMOD = u16;

namespace KeyBindings {
extern i32 old_keycode_to_sdl_scancode(i32 key);
extern SCANCODE keycode_to_scancode(KEYCODE keycode);
extern KEYCODE scancode_to_keycode(SCANCODE scancode);

/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

// This is copied from SDL to avoid transitively including SDL headers.
// Names are changed to avoid clashing, but it must match exactly with the true definitions for SDL_Scancode/SDL_Keycode/SDL_Keymod.
#define MC_SCANCODE_MASK (1u << 30)
#define MC_SCANCODE_TO_KEYCODE(sc) ((sc) | MC_SCANCODE_MASK)
}  // namespace KeyBindings

extern "C" {
// NOLINTNEXTLINE(performance-enum-size)
typedef enum MC_Scancode : unsigned int {
    MC_SCANCODE_UNKNOWN = 0,

    /**
     *  \name Usage page 0x07
     *
     *  These values are from usage page 0x07 (USB keyboard page).
     */
    /* @{ */

    MC_SCANCODE_A = 4,
    MC_SCANCODE_B = 5,
    MC_SCANCODE_C = 6,
    MC_SCANCODE_D = 7,
    MC_SCANCODE_E = 8,
    MC_SCANCODE_F = 9,
    MC_SCANCODE_G = 10,
    MC_SCANCODE_H = 11,
    MC_SCANCODE_I = 12,
    MC_SCANCODE_J = 13,
    MC_SCANCODE_K = 14,
    MC_SCANCODE_L = 15,
    MC_SCANCODE_M = 16,
    MC_SCANCODE_N = 17,
    MC_SCANCODE_O = 18,
    MC_SCANCODE_P = 19,
    MC_SCANCODE_Q = 20,
    MC_SCANCODE_R = 21,
    MC_SCANCODE_S = 22,
    MC_SCANCODE_T = 23,
    MC_SCANCODE_U = 24,
    MC_SCANCODE_V = 25,
    MC_SCANCODE_W = 26,
    MC_SCANCODE_X = 27,
    MC_SCANCODE_Y = 28,
    MC_SCANCODE_Z = 29,

    MC_SCANCODE_1 = 30,
    MC_SCANCODE_2 = 31,
    MC_SCANCODE_3 = 32,
    MC_SCANCODE_4 = 33,
    MC_SCANCODE_5 = 34,
    MC_SCANCODE_6 = 35,
    MC_SCANCODE_7 = 36,
    MC_SCANCODE_8 = 37,
    MC_SCANCODE_9 = 38,
    MC_SCANCODE_0 = 39,

    MC_SCANCODE_RETURN = 40,
    MC_SCANCODE_ESCAPE = 41,
    MC_SCANCODE_BACKSPACE = 42,
    MC_SCANCODE_TAB = 43,
    MC_SCANCODE_SPACE = 44,

    MC_SCANCODE_MINUS = 45,
    MC_SCANCODE_EQUALS = 46,
    MC_SCANCODE_LEFTBRACKET = 47,
    MC_SCANCODE_RIGHTBRACKET = 48,
    MC_SCANCODE_BACKSLASH = 49, /**< Located at the lower left of the return
                                  *   key on ISO keyboards and at the right end
                                  *   of the QWERTY row on ANSI keyboards.
                                  *   Produces REVERSE SOLIDUS (backslash) and
                                  *   VERTICAL LINE in a US layout, REVERSE
                                  *   SOLIDUS and VERTICAL LINE in a UK Mac
                                  *   layout, NUMBER SIGN and TILDE in a UK
                                  *   Windows layout, DOLLAR SIGN and POUND SIGN
                                  *   in a Swiss German layout, NUMBER SIGN and
                                  *   APOSTROPHE in a German layout, GRAVE
                                  *   ACCENT and POUND SIGN in a French Mac
                                  *   layout, and ASTERISK and MICRO SIGN in a
                                  *   French Windows layout.
                                  */
    MC_SCANCODE_NONUSHASH = 50, /**< ISO USB keyboards actually use this code
                                  *   instead of 49 for the same key, but all
                                  *   OSes I've seen treat the two codes
                                  *   identically. So, as an implementor, unless
                                  *   your keyboard generates both of those
                                  *   codes and your OS treats them differently,
                                  *   you should generate MC_SCANCODE_BACKSLASH
                                  *   instead of this code. As a user, you
                                  *   should not rely on this code because SDL
                                  *   will never generate it with most (all?)
                                  *   keyboards.
                                  */
    MC_SCANCODE_SEMICOLON = 51,
    MC_SCANCODE_APOSTROPHE = 52,
    MC_SCANCODE_GRAVE = 53, /**< Located in the top left corner (on both ANSI
                              *   and ISO keyboards). Produces GRAVE ACCENT and
                              *   TILDE in a US Windows layout and in US and UK
                              *   Mac layouts on ANSI keyboards, GRAVE ACCENT
                              *   and NOT SIGN in a UK Windows layout, SECTION
                              *   SIGN and PLUS-MINUS SIGN in US and UK Mac
                              *   layouts on ISO keyboards, SECTION SIGN and
                              *   DEGREE SIGN in a Swiss German layout (Mac:
                              *   only on ISO keyboards), CIRCUMFLEX ACCENT and
                              *   DEGREE SIGN in a German layout (Mac: only on
                              *   ISO keyboards), SUPERSCRIPT TWO and TILDE in a
                              *   French Windows layout, COMMERCIAL AT and
                              *   NUMBER SIGN in a French Mac layout on ISO
                              *   keyboards, and LESS-THAN SIGN and GREATER-THAN
                              *   SIGN in a Swiss German, German, or French Mac
                              *   layout on ANSI keyboards.
                              */
    MC_SCANCODE_COMMA = 54,
    MC_SCANCODE_PERIOD = 55,
    MC_SCANCODE_SLASH = 56,

    MC_SCANCODE_CAPSLOCK = 57,

    MC_SCANCODE_F1 = 58,
    MC_SCANCODE_F2 = 59,
    MC_SCANCODE_F3 = 60,
    MC_SCANCODE_F4 = 61,
    MC_SCANCODE_F5 = 62,
    MC_SCANCODE_F6 = 63,
    MC_SCANCODE_F7 = 64,
    MC_SCANCODE_F8 = 65,
    MC_SCANCODE_F9 = 66,
    MC_SCANCODE_F10 = 67,
    MC_SCANCODE_F11 = 68,
    MC_SCANCODE_F12 = 69,

    MC_SCANCODE_PRINTSCREEN = 70,
    MC_SCANCODE_SCROLLLOCK = 71,
    MC_SCANCODE_PAUSE = 72,
    MC_SCANCODE_INSERT = 73, /**< insert on PC, help on some Mac keyboards (but
                                   does send code 73, not 117) */
    MC_SCANCODE_HOME = 74,
    MC_SCANCODE_PAGEUP = 75,
    MC_SCANCODE_DELETE = 76,
    MC_SCANCODE_END = 77,
    MC_SCANCODE_PAGEDOWN = 78,
    MC_SCANCODE_RIGHT = 79,
    MC_SCANCODE_LEFT = 80,
    MC_SCANCODE_DOWN = 81,
    MC_SCANCODE_UP = 82,

    MC_SCANCODE_NUMLOCKCLEAR = 83, /**< num lock on PC, clear on Mac keyboards
                                     */
    MC_SCANCODE_KP_DIVIDE = 84,
    MC_SCANCODE_KP_MULTIPLY = 85,
    MC_SCANCODE_KP_MINUS = 86,
    MC_SCANCODE_KP_PLUS = 87,
    MC_SCANCODE_KP_ENTER = 88,
    MC_SCANCODE_KP_1 = 89,
    MC_SCANCODE_KP_2 = 90,
    MC_SCANCODE_KP_3 = 91,
    MC_SCANCODE_KP_4 = 92,
    MC_SCANCODE_KP_5 = 93,
    MC_SCANCODE_KP_6 = 94,
    MC_SCANCODE_KP_7 = 95,
    MC_SCANCODE_KP_8 = 96,
    MC_SCANCODE_KP_9 = 97,
    MC_SCANCODE_KP_0 = 98,
    MC_SCANCODE_KP_PERIOD = 99,

    MC_SCANCODE_NONUSBACKSLASH = 100, /**< This is the additional key that ISO
                                        *   keyboards have over ANSI ones,
                                        *   located between left shift and Z.
                                        *   Produces GRAVE ACCENT and TILDE in a
                                        *   US or UK Mac layout, REVERSE SOLIDUS
                                        *   (backslash) and VERTICAL LINE in a
                                        *   US or UK Windows layout, and
                                        *   LESS-THAN SIGN and GREATER-THAN SIGN
                                        *   in a Swiss German, German, or French
                                        *   layout. */
    MC_SCANCODE_APPLICATION = 101,    /**< windows contextual menu, compose */
    MC_SCANCODE_POWER = 102,          /**< The USB document says this is a status flag,
                               *   not a physical key - but some Mac keyboards
                               *   do have a power key. */
    MC_SCANCODE_KP_EQUALS = 103,
    MC_SCANCODE_F13 = 104,
    MC_SCANCODE_F14 = 105,
    MC_SCANCODE_F15 = 106,
    MC_SCANCODE_F16 = 107,
    MC_SCANCODE_F17 = 108,
    MC_SCANCODE_F18 = 109,
    MC_SCANCODE_F19 = 110,
    MC_SCANCODE_F20 = 111,
    MC_SCANCODE_F21 = 112,
    MC_SCANCODE_F22 = 113,
    MC_SCANCODE_F23 = 114,
    MC_SCANCODE_F24 = 115,
    MC_SCANCODE_EXECUTE = 116,
    MC_SCANCODE_HELP = 117, /**< AL Integrated Help Center */
    MC_SCANCODE_MENU = 118, /**< Menu (show menu) */
    MC_SCANCODE_SELECT = 119,
    MC_SCANCODE_STOP = 120,  /**< AC Stop */
    MC_SCANCODE_AGAIN = 121, /**< AC Redo/Repeat */
    MC_SCANCODE_UNDO = 122,  /**< AC Undo */
    MC_SCANCODE_CUT = 123,   /**< AC Cut */
    MC_SCANCODE_COPY = 124,  /**< AC Copy */
    MC_SCANCODE_PASTE = 125, /**< AC Paste */
    MC_SCANCODE_FIND = 126,  /**< AC Find */
    MC_SCANCODE_MUTE = 127,
    MC_SCANCODE_VOLUMEUP = 128,
    MC_SCANCODE_VOLUMEDOWN = 129,
    /* not sure whether there's a reason to enable these */
    /*     MC_SCANCODE_LOCKINGCAPSLOCK = 130,  */
    /*     MC_SCANCODE_LOCKINGNUMLOCK = 131, */
    /*     MC_SCANCODE_LOCKINGSCROLLLOCK = 132, */
    MC_SCANCODE_KP_COMMA = 133,
    MC_SCANCODE_KP_EQUALSAS400 = 134,

    MC_SCANCODE_INTERNATIONAL1 = 135, /**< used on Asian keyboards, see
                                            footnotes in USB doc */
    MC_SCANCODE_INTERNATIONAL2 = 136,
    MC_SCANCODE_INTERNATIONAL3 = 137, /**< Yen */
    MC_SCANCODE_INTERNATIONAL4 = 138,
    MC_SCANCODE_INTERNATIONAL5 = 139,
    MC_SCANCODE_INTERNATIONAL6 = 140,
    MC_SCANCODE_INTERNATIONAL7 = 141,
    MC_SCANCODE_INTERNATIONAL8 = 142,
    MC_SCANCODE_INTERNATIONAL9 = 143,
    MC_SCANCODE_LANG1 = 144, /**< Hangul/English toggle */
    MC_SCANCODE_LANG2 = 145, /**< Hanja conversion */
    MC_SCANCODE_LANG3 = 146, /**< Katakana */
    MC_SCANCODE_LANG4 = 147, /**< Hiragana */
    MC_SCANCODE_LANG5 = 148, /**< Zenkaku/Hankaku */
    MC_SCANCODE_LANG6 = 149, /**< reserved */
    MC_SCANCODE_LANG7 = 150, /**< reserved */
    MC_SCANCODE_LANG8 = 151, /**< reserved */
    MC_SCANCODE_LANG9 = 152, /**< reserved */

    MC_SCANCODE_ALTERASE = 153, /**< Erase-Eaze */
    MC_SCANCODE_SYSREQ = 154,
    MC_SCANCODE_CANCEL = 155, /**< AC Cancel */
    MC_SCANCODE_CLEAR = 156,
    MC_SCANCODE_PRIOR = 157,
    MC_SCANCODE_RETURN2 = 158,
    MC_SCANCODE_SEPARATOR = 159,
    MC_SCANCODE_OUT = 160,
    MC_SCANCODE_OPER = 161,
    MC_SCANCODE_CLEARAGAIN = 162,
    MC_SCANCODE_CRSEL = 163,
    MC_SCANCODE_EXSEL = 164,

    MC_SCANCODE_KP_00 = 176,
    MC_SCANCODE_KP_000 = 177,
    MC_SCANCODE_THOUSANDSSEPARATOR = 178,
    MC_SCANCODE_DECIMALSEPARATOR = 179,
    MC_SCANCODE_CURRENCYUNIT = 180,
    MC_SCANCODE_CURRENCYSUBUNIT = 181,
    MC_SCANCODE_KP_LEFTPAREN = 182,
    MC_SCANCODE_KP_RIGHTPAREN = 183,
    MC_SCANCODE_KP_LEFTBRACE = 184,
    MC_SCANCODE_KP_RIGHTBRACE = 185,
    MC_SCANCODE_KP_TAB = 186,
    MC_SCANCODE_KP_BACKSPACE = 187,
    MC_SCANCODE_KP_A = 188,
    MC_SCANCODE_KP_B = 189,
    MC_SCANCODE_KP_C = 190,
    MC_SCANCODE_KP_D = 191,
    MC_SCANCODE_KP_E = 192,
    MC_SCANCODE_KP_F = 193,
    MC_SCANCODE_KP_XOR = 194,
    MC_SCANCODE_KP_POWER = 195,
    MC_SCANCODE_KP_PERCENT = 196,
    MC_SCANCODE_KP_LESS = 197,
    MC_SCANCODE_KP_GREATER = 198,
    MC_SCANCODE_KP_AMPERSAND = 199,
    MC_SCANCODE_KP_DBLAMPERSAND = 200,
    MC_SCANCODE_KP_VERTICALBAR = 201,
    MC_SCANCODE_KP_DBLVERTICALBAR = 202,
    MC_SCANCODE_KP_COLON = 203,
    MC_SCANCODE_KP_HASH = 204,
    MC_SCANCODE_KP_SPACE = 205,
    MC_SCANCODE_KP_AT = 206,
    MC_SCANCODE_KP_EXCLAM = 207,
    MC_SCANCODE_KP_MEMSTORE = 208,
    MC_SCANCODE_KP_MEMRECALL = 209,
    MC_SCANCODE_KP_MEMCLEAR = 210,
    MC_SCANCODE_KP_MEMADD = 211,
    MC_SCANCODE_KP_MEMSUBTRACT = 212,
    MC_SCANCODE_KP_MEMMULTIPLY = 213,
    MC_SCANCODE_KP_MEMDIVIDE = 214,
    MC_SCANCODE_KP_PLUSMINUS = 215,
    MC_SCANCODE_KP_CLEAR = 216,
    MC_SCANCODE_KP_CLEARENTRY = 217,
    MC_SCANCODE_KP_BINARY = 218,
    MC_SCANCODE_KP_OCTAL = 219,
    MC_SCANCODE_KP_DECIMAL = 220,
    MC_SCANCODE_KP_HEXADECIMAL = 221,

    MC_SCANCODE_LCTRL = 224,
    MC_SCANCODE_LSHIFT = 225,
    MC_SCANCODE_LALT = 226, /**< alt, option */
    MC_SCANCODE_LGUI = 227, /**< windows, command (apple), meta */
    MC_SCANCODE_RCTRL = 228,
    MC_SCANCODE_RSHIFT = 229,
    MC_SCANCODE_RALT = 230, /**< alt gr, option */
    MC_SCANCODE_RGUI = 231, /**< windows, command (apple), meta */

    MC_SCANCODE_MODE = 257, /**< I'm not sure if this is really not covered
                                 *   by any of the above, but since there's a
                                 *   special SDL_KMOD_MODE for it I'm adding it here
                                 */

    /* @} */ /* Usage page 0x07 */

    /**
     *  \name Usage page 0x0C
     *
     *  These values are mapped from usage page 0x0C (USB consumer page).
     *
     *  There are way more keys in the spec than we can represent in the
     *  current scancode range, so pick the ones that commonly come up in
     *  real world usage.
     */
    /* @{ */

    MC_SCANCODE_SLEEP = 258, /**< Sleep */
    MC_SCANCODE_WAKE = 259,  /**< Wake */

    MC_SCANCODE_CHANNEL_INCREMENT = 260, /**< Channel Increment */
    MC_SCANCODE_CHANNEL_DECREMENT = 261, /**< Channel Decrement */

    MC_SCANCODE_MEDIA_PLAY = 262,           /**< Play */
    MC_SCANCODE_MEDIA_PAUSE = 263,          /**< Pause */
    MC_SCANCODE_MEDIA_RECORD = 264,         /**< Record */
    MC_SCANCODE_MEDIA_FAST_FORWARD = 265,   /**< Fast Forward */
    MC_SCANCODE_MEDIA_REWIND = 266,         /**< Rewind */
    MC_SCANCODE_MEDIA_NEXT_TRACK = 267,     /**< Next Track */
    MC_SCANCODE_MEDIA_PREVIOUS_TRACK = 268, /**< Previous Track */
    MC_SCANCODE_MEDIA_STOP = 269,           /**< Stop */
    MC_SCANCODE_MEDIA_EJECT = 270,          /**< Eject */
    MC_SCANCODE_MEDIA_PLAY_PAUSE = 271,     /**< Play / Pause */
    MC_SCANCODE_MEDIA_SELECT = 272,         /* Media Select */

    MC_SCANCODE_AC_NEW = 273,        /**< AC New */
    MC_SCANCODE_AC_OPEN = 274,       /**< AC Open */
    MC_SCANCODE_AC_CLOSE = 275,      /**< AC Close */
    MC_SCANCODE_AC_EXIT = 276,       /**< AC Exit */
    MC_SCANCODE_AC_SAVE = 277,       /**< AC Save */
    MC_SCANCODE_AC_PRINT = 278,      /**< AC Print */
    MC_SCANCODE_AC_PROPERTIES = 279, /**< AC Properties */

    MC_SCANCODE_AC_SEARCH = 280,    /**< AC Search */
    MC_SCANCODE_AC_HOME = 281,      /**< AC Home */
    MC_SCANCODE_AC_BACK = 282,      /**< AC Back */
    MC_SCANCODE_AC_FORWARD = 283,   /**< AC Forward */
    MC_SCANCODE_AC_STOP = 284,      /**< AC Stop */
    MC_SCANCODE_AC_REFRESH = 285,   /**< AC Refresh */
    MC_SCANCODE_AC_BOOKMARKS = 286, /**< AC Bookmarks */

    /* @} */ /* Usage page 0x0C */

    /**
     *  \name Mobile keys
     *
     *  These are values that are often used on mobile phones.
     */
    /* @{ */

    MC_SCANCODE_SOFTLEFT = 287,  /**< Usually situated below the display on phones and
                                      used as a multi-function feature key for selecting
                                      a software defined function shown on the bottom left
                                      of the display. */
    MC_SCANCODE_SOFTRIGHT = 288, /**< Usually situated below the display on phones and
                                       used as a multi-function feature key for selecting
                                       a software defined function shown on the bottom right
                                       of the display. */
    MC_SCANCODE_CALL = 289,      /**< Used for accepting phone calls. */
    MC_SCANCODE_ENDCALL = 290,   /**< Used for rejecting phone calls. */

    /* @} */ /* Mobile keys */

    /* Add any other keys here. */

    MC_SCANCODE_RESERVED = 400, /**< 400-500 reserved for dynamic keycodes */

    MC_SCANCODE_COUNT = 512 /**< not a key, just marks the number of scancodes for array bounds */

} MC_Scancode;
}  // extern "C"

// alphabet
#define KEY_A MC_SCANCODE_A
#define KEY_B MC_SCANCODE_B
#define KEY_C MC_SCANCODE_C
#define KEY_D MC_SCANCODE_D
#define KEY_E MC_SCANCODE_E
#define KEY_F MC_SCANCODE_F
#define KEY_G MC_SCANCODE_G
#define KEY_H MC_SCANCODE_H
#define KEY_I MC_SCANCODE_I
#define KEY_J MC_SCANCODE_J
#define KEY_K MC_SCANCODE_K
#define KEY_L MC_SCANCODE_L
#define KEY_M MC_SCANCODE_M
#define KEY_N MC_SCANCODE_N
#define KEY_O MC_SCANCODE_O
#define KEY_P MC_SCANCODE_P
#define KEY_Q MC_SCANCODE_Q
#define KEY_R MC_SCANCODE_R
#define KEY_S MC_SCANCODE_S
#define KEY_T MC_SCANCODE_T
#define KEY_U MC_SCANCODE_U
#define KEY_V MC_SCANCODE_V
#define KEY_W MC_SCANCODE_W
#define KEY_X MC_SCANCODE_X
#define KEY_Y MC_SCANCODE_Y
#define KEY_Z MC_SCANCODE_Z

// number (row)
#define KEY_0 MC_SCANCODE_0
#define KEY_1 MC_SCANCODE_1
#define KEY_2 MC_SCANCODE_2
#define KEY_3 MC_SCANCODE_3
#define KEY_4 MC_SCANCODE_4
#define KEY_5 MC_SCANCODE_5
#define KEY_6 MC_SCANCODE_6
#define KEY_7 MC_SCANCODE_7
#define KEY_8 MC_SCANCODE_8
#define KEY_9 MC_SCANCODE_9
#define KEY_MINUS MC_SCANCODE_MINUS
#define KEY_EQUALS MC_SCANCODE_EQUALS
#define KEY_COMMA MC_SCANCODE_COMMA
#define KEY_PERIOD MC_SCANCODE_PERIOD

// numpad
#define KEY_NUMPAD0 MC_SCANCODE_KP_0
#define KEY_NUMPAD1 MC_SCANCODE_KP_1
#define KEY_NUMPAD2 MC_SCANCODE_KP_2
#define KEY_NUMPAD3 MC_SCANCODE_KP_3
#define KEY_NUMPAD4 MC_SCANCODE_KP_4
#define KEY_NUMPAD5 MC_SCANCODE_KP_5
#define KEY_NUMPAD6 MC_SCANCODE_KP_6
#define KEY_NUMPAD7 MC_SCANCODE_KP_7
#define KEY_NUMPAD8 MC_SCANCODE_KP_8
#define KEY_NUMPAD9 MC_SCANCODE_KP_9
#define KEY_NUMPAD_MULTIPLY MC_SCANCODE_KP_MULTIPLY
#define KEY_NUMPAD_ADD MC_SCANCODE_KP_PLUS
#define KEY_NUMPAD_SEPARATOR MC_SCANCODE_KP_EQUALS
#define KEY_NUMPAD_SUBTRACT MC_SCANCODE_KP_MINUS
#define KEY_NUMPAD_DECIMAL MC_SCANCODE_KP_DECIMAL
#define KEY_NUMPAD_DIVIDE MC_SCANCODE_KP_DIVIDE

// function keys
#define KEY_F1 MC_SCANCODE_F1
#define KEY_F2 MC_SCANCODE_F2
#define KEY_F3 MC_SCANCODE_F3
#define KEY_F4 MC_SCANCODE_F4
#define KEY_F5 MC_SCANCODE_F5
#define KEY_F6 MC_SCANCODE_F6
#define KEY_F7 MC_SCANCODE_F7
#define KEY_F8 MC_SCANCODE_F8
#define KEY_F9 MC_SCANCODE_F9
#define KEY_F10 MC_SCANCODE_F10
#define KEY_F11 MC_SCANCODE_F11
#define KEY_F12 MC_SCANCODE_F12

// arrow keys
#define KEY_LEFT MC_SCANCODE_LEFT
#define KEY_RIGHT MC_SCANCODE_RIGHT
#define KEY_UP MC_SCANCODE_UP
#define KEY_DOWN MC_SCANCODE_DOWN

// special keys
#define KEY_TAB MC_SCANCODE_TAB
#define KEY_NUMPAD_ENTER MC_SCANCODE_KP_ENTER
#define KEY_ENTER MC_SCANCODE_RETURN
#define KEY_LSHIFT MC_SCANCODE_LSHIFT
#define KEY_RSHIFT MC_SCANCODE_RSHIFT
#define KEY_LCONTROL MC_SCANCODE_LCTRL
#define KEY_RCONTROL MC_SCANCODE_RCTRL
#define KEY_LALT MC_SCANCODE_LALT
#define KEY_RALT MC_SCANCODE_RALT
#define KEY_ESCAPE MC_SCANCODE_ESCAPE
#define KEY_TILDE MC_SCANCODE_GRAVE
#define KEY_SPACE MC_SCANCODE_SPACE
#define KEY_BACKSPACE MC_SCANCODE_BACKSPACE
#define KEY_END MC_SCANCODE_END
#define KEY_INSERT MC_SCANCODE_INSERT
#define KEY_DELETE MC_SCANCODE_DELETE
#define KEY_HELP MC_SCANCODE_HELP
#define KEY_HOME MC_SCANCODE_HOME
#define KEY_LSUPER MC_SCANCODE_LGUI
#define KEY_RSUPER MC_SCANCODE_RGUI
#define KEY_PAGEUP MC_SCANCODE_PAGEUP
#define KEY_PAGEDOWN MC_SCANCODE_PAGEDOWN

// media keys
#define KEY_PLAY MC_SCANCODE_MEDIA_PLAY
#define KEY_PAUSE MC_SCANCODE_MEDIA_PAUSE
#define KEY_PLAYPAUSE MC_SCANCODE_MEDIA_PLAY_PAUSE
#define KEY_STOP MC_SCANCODE_MEDIA_STOP
#define KEY_PREV MC_SCANCODE_MEDIA_PREVIOUS_TRACK
#define KEY_NEXT MC_SCANCODE_MEDIA_NEXT_TRACK
#define KEY_MUTE MC_SCANCODE_MUTE
#define KEY_VOLUMEDOWN MC_SCANCODE_VOLUMEDOWN
#define KEY_VOLUMEUP MC_SCANCODE_VOLUMEUP

// modifier keys
#define KEYMOD_NONE (KEYMOD)0x0000u     /**< no modifier is applicable. */
#define KEYMOD_LSHIFT (KEYMOD)0x0001u   /**< the left Shift key is down. */
#define KEYMOD_RSHIFT (KEYMOD)0x0002u   /**< the right Shift key is down. */
#define KEYMOD_LEVEL5 (KEYMOD)0x0004u   /**< the Level 5 Shift key is down. */
#define KEYMOD_LCONTROL (KEYMOD)0x0040u /**< the left Ctrl (Control) key is down. */
#define KEYMOD_RCONTROL (KEYMOD)0x0080u /**< the right Ctrl (Control) key is down. */
#define KEYMOD_LALT (KEYMOD)0x0100u     /**< the left Alt key is down. */
#define KEYMOD_RALT (KEYMOD)0x0200u     /**< the right Alt key is down. */
#define KEYMOD_LSUPER (KEYMOD)0x0400u   /**< the left GUI key (often the Windows key) is down. */
#define KEYMOD_RSUPER (KEYMOD)0x0800u   /**< the right GUI key (often the Windows key) is down. */
#define KEYMOD_NUM (KEYMOD)0x1000u      /**< the Num Lock key (may be located on an extended keypad) is down. */
#define KEYMOD_CAPS (KEYMOD)0x2000u     /**< the Caps Lock key is down. */
#define KEYMOD_MODE (KEYMOD)0x4000u     /**< the !AltGr key is down. */
#define KEYMOD_SCROLL (KEYMOD)0x8000u   /**< the Scroll Lock key is down. */
#define KEYMOD_CONTROL (KEYMOD)(KEYMOD_LCONTROL | KEYMOD_RCONTROL) /**< Any Ctrl key is down. */
#define KEYMOD_SHIFT (KEYMOD)(KEYMOD_LSHIFT | KEYMOD_RSHIFT)       /**< Any Shift key is down. */
#define KEYMOD_ALT (KEYMOD)(KEYMOD_LALT | KEYMOD_RALT)             /**< Any Alt key is down. */
#define KEYMOD_SUPER (KEYMOD)(KEYMOD_LSUPER | KEYMOD_RSUPER)       /**< Any GUI key is down. */
