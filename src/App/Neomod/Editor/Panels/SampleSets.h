#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "HitSounds.h"
#include "Registration.h"
#include "SamplePreview.h"
#include "noinclude.h"
#include "types.h"

#include <array>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class CBaseUIStyledLabel;
struct EditorState;

// a popup showing the samples the shown map has for one custom sample index: a cell for each sample set and sound, with
// the file the map folder has for it (none: the skin sample plays). a cell picked is heard: its file, or what the game
// plays without one. Choose File... copies a sound file into the folder under the cell name, in place of the file the
// cell had. Delete File deletes that file. a click outside or Escape closes the popup
class SampleSets final : public CBaseUIBox {
    NOCOPY_NOMOVE(SampleSets)
   public:
    // the sounds a sample set has, as files name them
    static constexpr std::array<std::string_view, 7> SOUNDS{"hitnormal",   "hitwhistle",    "hitfinish", "hitclap",
                                                            "sliderslide", "sliderwhistle", "slidertick"};
    static constexpr uSz SETS{3};  // normal, soft, drum
    static constexpr i32 MAX_INDEX{100};

    // shows `index` first (1 to MAX_INDEX); `folderChanged` is called when a file came into the map folder or went
    SampleSets(EditorState &state, CBaseUIPopupLayer &popups, i32 index, std::function<void()> folderChanged);
    ~SampleSets() override;

    // shows it at `pos` (the layer owns it from here). returns the layer id
    u32 open(vec2 pos);
    // the sound file at `path` becomes the sample for the picked cell (what Choose File... does with the one chosen);
    // an osu!stable map set is copied into the neomod library first, and the file goes into that
    void setFile(const std::string &path);

    // which file in `files` (from one folder) is the sample for a set (0 normal, 1 soft, 2 drum), a sound (from SOUNDS)
    // and an index: the first among .wav, .mp3 and .ogg with its name, in any case (empty: none)
    [[nodiscard]] static std::string_view fileFor(std::span<const std::string> files, uSz set, uSz sound, i32 index);
    // the indices `files` have samples for, in order
    [[nodiscard]] static std::vector<i32> indicesIn(std::span<const std::string> files);

    void tick() override;
    void draw() override;

   private:
    // reads the folder again: what the cells have, and which indices have files
    void scan();
    // the cells, the line for the picked one and Delete File as they are now
    void show();
    // what the file for the picked cell is called, without its extension
    [[nodiscard]] std::string pickedName() const;
    void setIndex(i32 index);
    void pick(uSz cell);
    void deleteFile();
    // plays what the game plays for the picked cell without a file of its own; a looping slider sound for a while
    void hearSkin();
    // ends what's being heard
    void hush();

    EditorState &state;
    CBaseUIPopupLayer &popups;
    std::function<void()> folderChanged;
    std::array<CBaseUIStyledButton *, SETS * SOUNDS.size()> cells{};  // the sounds per set, set after set
    std::array<std::string, SETS * SOUNDS.size()> files;              // what each has (empty: none)
    CBaseUIStyledLabel *used;
    CBaseUIStyledLabel *detail;
    CBaseUIStyledButton *deleteButton;
    std::string read;    // the map folder as named by its record, when it was read
    std::string folder;  // ...with a slash at its end
    i32 index;
    uSz picked{0};
    SamplePreview preview;
    std::vector<neomod::HitSoundUtils::Set_Slider_Hit> looping;  // the skin slider sounds hearSkin() started
    f64 loopingUntil{0.0};                                       // when they stop (engine time)
    Mc::Registration fileDialog;  // the Choose File dialog, whose answer goes nowhere once the popup is gone
    u32 id{0};                    // owned by the popup layer
};
