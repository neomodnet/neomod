#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"
#include "VertexArrayObject.h"
#include "noinclude.h"
#include "types.h"

#include <optional>

struct EditorState;

// the whole song: kiai sections, breaks, the preview point, bookmarks (below the middle), timing points (above it) and
// the time. a click seeks there, a drag follows the cursor (every 125 ms while playing)
class Overview final : public CBaseUIElement {
    NOCOPY_NOMOVE(Overview)
   public:
    explicit Overview(EditorState &state);
    ~Overview() override;

    void draw() override;

   protected:
    void onMouseDownInside(bool left, bool right) override;
    void onCapturedMouseMove() override;

   private:
    void seekToCursor();
    // the song time under the cursor (none while its length isn't known)
    [[nodiscard]] std::optional<i32> cursorTime() const;
    [[nodiscard]] f32 xAt(f64 timeMS) const;

    EditorState &state;
    f64 lastSeek{0.0};            // engine time
    std::optional<i32> seekedTo;  // ...to
    VertexArrayObject rects;      // what draw() draws in one go (kept for its capacity)
};
