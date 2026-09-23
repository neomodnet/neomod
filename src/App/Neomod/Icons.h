// Copyright (c) 2018, PG, All rights reserved.
#ifndef OSUICONS_H
#define OSUICONS_H

#include <array>

namespace Icons {

inline constexpr char32_t Z_UNKNOWN_CHAR{U'?'};
inline constexpr char32_t Z_SPACE{0x0020};
inline constexpr char32_t GEAR{0xf013};
inline constexpr char32_t DESKTOP{0xf108};
inline constexpr char32_t CIRCLE{0xf10c};
inline constexpr char32_t CUBE{0xf1b2};
inline constexpr char32_t VOLUME_UP{0xf028};
inline constexpr char32_t VOLUME_DOWN{0xf027};
inline constexpr char32_t VOLUME_OFF{0xf026};
inline constexpr char32_t PAINTBRUSH{0xf1fc};
inline constexpr char32_t GAMEPAD{0xf11b};
inline constexpr char32_t WRENCH{0xf0ad};
inline constexpr char32_t EYE{0xf06e};
inline constexpr char32_t ARROW_CIRCLE_UP{0xf01b};
inline constexpr char32_t TROPHY{0xf091};
inline constexpr char32_t CARET_DOWN{0xf0d7};
inline constexpr char32_t ARROW_DOWN{0xf063};
inline constexpr char32_t GLOBE{0xf0ac};
inline constexpr char32_t USER{0xf2be};
inline constexpr char32_t UNDO{0xf0e2};
inline constexpr char32_t KEYBOARD{0xf11c};
inline constexpr char32_t LOCK{0xf023};
inline constexpr char32_t UNLOCK{0xf09c};
inline constexpr char32_t DISCORD{0xf2ef};
inline constexpr char32_t TWITTER{0xf099};
inline constexpr char32_t PLAY{0xf04b};
inline constexpr char32_t STOP{0xf04d};

inline constexpr const std::array icons{
    Z_UNKNOWN_CHAR,   //
    Z_SPACE,          //
    GEAR,             //
    DESKTOP,          //
    CIRCLE,           //
    CUBE,             //
    VOLUME_UP,        //
    VOLUME_DOWN,      //
    VOLUME_OFF,       //
    PAINTBRUSH,       //
    GAMEPAD,          //
    WRENCH,           //
    EYE,              //
    ARROW_CIRCLE_UP,  //
    TROPHY,           //
    CARET_DOWN,       //
    ARROW_DOWN,       //
    GLOBE,            //
    USER,             //
    UNDO,             //
    KEYBOARD,         //
    LOCK,             //
    UNLOCK,           //
    DISCORD,          //
    TWITTER,          //
    PLAY,             //
    STOP,             //
};

};  // namespace Icons

#endif
