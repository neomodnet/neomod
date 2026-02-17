#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "HUD.h"
#include "UIAvatar.h"

#include <array>

class ScoreboardSlot final {
    NOCOPY_NOMOVE(ScoreboardSlot)
   public:
    ScoreboardSlot(const SCORE_ENTRY& score, int index);
    ~ScoreboardSlot();

    void draw();
    void updateIndex(int new_index, bool is_player, bool animate);
    SCORE_ENTRY score;

   private:
    // draw() helpers
    enum SlotColType : u8 { BKGND, COMBOACC, OTHER, COLTYPE_MAX };
    enum SlotColEffect : u8 { DEFAULT, HIGHLIGHT, TOP, DEAD, FRIEND, COLEFFECT_MAX };

    static constexpr const std::array<std::array<Color, COLEFFECT_MAX>, COLTYPE_MAX> slot_colors{
        std::array<Color, COLEFFECT_MAX>{
            // BKGND
            Color{0xff114459},  // DEFAULT
            Color{0xff777777},  // HIGHLIGHT
            Color{0xff1b6a8c},  // TOP
            Color{0xff660000},  // DEAD
            Color{0xff9C205C},  // FRIEND
        },
        std::array<Color, COLEFFECT_MAX>{
            // COMBOACC
            Color{0xff5d9ca1},  // DEFAULT
            Color{0xff99fafe},  // HIGHLIGHT
            Color{0xff84dbe0},  // TOP
            Color{0xff5d9ca1},  // DEAD (unused, same as default)
            Color{0xff5d9ca1},  // FRIEND  (unused, same as default)
        },
        std::array<Color, COLEFFECT_MAX>{
            // OTHER
            Color{0xffaaaaaa},  // DEFAULT
            Color{0xffffffff},  // HIGHLIGHT
            Color{0xffeeeeee},  // TOP
            Color{0xffee0000},  // DEAD
            Color{0xffaaaaaa},  // FRIEND  (unused, same as default)
        }};

    [[nodiscard]] forceinline SlotColEffect getCurSlotEffect() const {
        if(this->score.dead) return DEAD;
        if(this->score.highlight) return HIGHLIGHT;
        if(this->index == 0) return TOP;
        if(this->is_friend) return FRIEND;
        return DEFAULT;
    }

    std::unique_ptr<UIAvatar> avatar{nullptr};

    int index;
    float y = 0.f;
    float fAlpha = 0.f;
    float fFlash = 0.f;
    bool is_friend = false;
    bool was_visible = false;
};
