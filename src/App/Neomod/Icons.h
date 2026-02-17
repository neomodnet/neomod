// Copyright (c) 2018, PG, All rights reserved.
#ifndef OSUICONS_H
#define OSUICONS_H

#include <array>

namespace Icons {

inline constexpr char16_t Z_UNKNOWN_CHAR{u'?'};
inline constexpr char16_t Z_SPACE{0x0020};
inline constexpr char16_t GEAR{0xf013};
inline constexpr char16_t DESKTOP{0xf108};
inline constexpr char16_t CIRCLE{0xf10c};
inline constexpr char16_t CUBE{0xf1b2};
inline constexpr char16_t VOLUME_UP{0xf028};
inline constexpr char16_t VOLUME_DOWN{0xf027};
inline constexpr char16_t VOLUME_OFF{0xf026};
inline constexpr char16_t PAINTBRUSH{0xf1fc};
inline constexpr char16_t GAMEPAD{0xf11b};
inline constexpr char16_t WRENCH{0xf0ad};
inline constexpr char16_t EYE{0xf06e};
inline constexpr char16_t ARROW_CIRCLE_UP{0xf01b};
inline constexpr char16_t TROPHY{0xf091};
inline constexpr char16_t CARET_DOWN{0xf0d7};
inline constexpr char16_t ARROW_DOWN{0xf063};
inline constexpr char16_t GLOBE{0xf0ac};
inline constexpr char16_t USER{0xf2be};
inline constexpr char16_t UNDO{0xf0e2};
inline constexpr char16_t KEYBOARD{0xf11c};
inline constexpr char16_t LOCK{0xf023};
inline constexpr char16_t UNLOCK{0xf09c};
inline constexpr char16_t DISCORD{0xf2ef};
inline constexpr char16_t TWITTER{0xf099};

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
};

};  // namespace Icons

#endif
