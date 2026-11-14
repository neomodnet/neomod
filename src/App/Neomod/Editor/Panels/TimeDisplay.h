#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CBaseUIStyledLabel;
class CBaseUIStyledTextbox;
struct EditorState;

// the editor time (mm:ss:mmm), how far into the song it is, and whether it's in a kiai section. a click on it types a
// time in instead: each change that reads as one (see parse()) goes to the caller at once, and Enter, Escape or the
// focus leaving ends it
class TimeDisplay final : public CBaseUIBox {
    NOCOPY_NOMOVE(TimeDisplay)
   public:
    // a time typed or pasted, and the combo numbers that came with it
    struct Timestamp {
        i32 time;
        std::vector<i32> combos;
    };

    TimeDisplay(EditorState &state, std::function<void(const Timestamp &)> jump);
    ~TimeDisplay() override;

    void tick() override;
    void onKeyDown(KeyboardEvent &e) override;
    // as wide as the widest time and progress the song can show, so neither is ever cut and its width stays put
    [[nodiscard]] vec2 getNaturalSize() override;

    // mm:ss:mmm, minutes past 59 as they are, a '-' before 0
    [[nodiscard]] static std::string format(i32 timeMS);
    // milliseconds, or mm:ss and mm:ss:mmm (minutes any width, '.' before the milliseconds too), with what osu!
    // timestamps add: combo numbers in parentheses, and " - " with anything after it
    [[nodiscard]] static std::optional<Timestamp> parse(std::string_view text);

   protected:
    void onMouseDownInside(bool left, bool right) override;

   private:
    // the field goes, the time shows again (what it has that wasn't taken yet is taken)
    void endTyping();

    EditorState &state;
    std::function<void(const Timestamp &)> jump;
    CBaseUIStyledLabel *time;
    CBaseUIStyledLabel *progress;
    CBaseUIStyledTextbox *field;
    std::string taken;  // what the field had when its user last heard of it
};
