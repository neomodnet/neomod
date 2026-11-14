// Copyright (c) 2026, WH, All rights reserved.
#include "SampleSets.h"

#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "EditorState.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Font.h"
#include "HitSounds.h"
#include "MapFiles.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "Parsing.h"
#include "SString.h"
#include "Skin.h"
#include "Stepper.h"
#include "UI.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

namespace {
// units, with room for "sliderwhistle"
constexpr f32 CELL_WIDTH{120.f};

// where SampleSets::SOUNDS has the slider sounds
constexpr uSz SLIDERSLIDE{4};
constexpr uSz SLIDERTICK{6};
static_assert(SampleSets::SOUNDS[SLIDERSLIDE] == "sliderslide" && SampleSets::SOUNDS[SLIDERTICK] == "slidertick");
// how long the looping skin slider sounds are heard
constexpr f64 LOOP_SECONDS{1.5};

// the sound file at `path` becomes the sample `stem` in the shown map folder, in place of the files that were it; false
// (said) if it can't
bool put(EditorState &state, const std::string &path, const std::string &stem,
         const std::function<void()> &folderChanged) {
    const std::string extension = SString::to_lower(Environment::getFileExtensionFromFilePath(path));
    if(!MapFiles::isSound(path)) {
        ui->getNotificationOverlay()->addNotification(tformat("{} can't be used: samples are .wav, .ogg or .mp3 files.",
                                                              Environment::getFileNameFromFilePath(path)));
        return false;
    }
    const std::string folder = MapFiles::folderOf(state.document->getLocation());
    const std::string name = fmt::format("{}.{}", stem, extension);
    // chosen from the folder itself, it's there already
    std::error_code ec;
    const auto target = File::getFsPath(folder + name);
    if(!std::filesystem::equivalent(File::getFsPath(path), target, ec) && !File::copy(path, folder + name)) {
        ui->getNotificationOverlay()->addToast(tformat("{} couldn't be copied into the map folder.", name),
                                               ERROR_TOAST);
        return false;
    }
    // the files that were the sample go once the new one is there (under another extension, or in another case)
    for(const std::string &file : Environment::getFilesInFolder(folder)) {
        const std::string_view other{file};
        const uSz dot = other.rfind('.');
        if(dot != std::string_view::npos && SString::strcase_equal(other.substr(0, dot), stem) &&
           MapFiles::isSound(other) && !std::filesystem::equivalent(File::getFsPath(folder + file), target, ec)) {
            Environment::deleteFile(folder + file);
        }
    }
    if(folderChanged) folderChanged();
    return true;
}
}  // namespace

SampleSets::SampleSets(EditorState &state, CBaseUIPopupLayer &popups, i32 index, std::function<void()> folderChanged)
    : CBaseUIBox(Axis::COLUMN, "editor_sample_sets"),
      state(state),
      popups(popups),
      folderChanged(std::move(folderChanged)),
      index(std::clamp(index, 1, MAX_INDEX)) {
    const UIStyle &style = uiStyle();
    // a press on its surface stays with it
    this->bClickThroughSelf = false;
    this->setPadding(style.metrics.padding * 2.f);
    this->setGap(style.metrics.gap);

    auto *heading = new CBaseUIStyledLabel(_("Custom Samples"));
    heading->setDim(true);
    auto *stepper = new Stepper(
        "editor_sample_sets_index", [this](i32 by) { this->setIndex(this->index + by); },
        [this] { return fmt::format("{}", this->index); });
    this->used = new CBaseUIStyledLabel({}, "editor_sample_sets_used");
    this->used->setDim(true);
    auto *top = new CBaseUIBox(Axis::ROW);
    top->setGap(style.metrics.gap);
    top->add(new CBaseUIStyledLabel(_("Sample Index")), BoxItem::fit())
        ->add(stepper, BoxItem::fixed(130.f))
        ->add(this->used, BoxItem::flex());

    const std::array<std::string, SETS> sets{_("Normal"), _("Soft"), _("Drum")};
    // one column as wide as the widest set name
    f32 labelWidth = 0.f;
    for(const std::string &set : sets) labelWidth = std::max(labelWidth, style.font()->getStringWidth(set));
    labelWidth /= style.getScale();
    const auto row = [&style, labelWidth](std::string label) {
        auto *line = new CBaseUIBox(Axis::ROW);
        line->setGap(style.metrics.gap);
        line->add(new CBaseUIStyledLabel(std::move(label)), BoxItem::fixed(labelWidth));
        return line;
    };
    // what the files are called
    CBaseUIBox *sounds = row({});
    for(const std::string_view sound : SOUNDS) {
        auto *name = new CBaseUIStyledLabel(std::string{sound});
        name->setDim(true);
        name->setJustification(TEXT_JUSTIFICATION::CENTERED);
        sounds->add(name, BoxItem::fixed(CELL_WIDTH));
    }
    this->add(heading, BoxItem::fit())->add(top, BoxItem::fit())->add(sounds, BoxItem::fit());
    for(uSz set = 0; set < SETS; set++) {
        CBaseUIBox *line = row(sets[set]);
        for(uSz sound = 0; sound < SOUNDS.size(); sound++) {
            const uSz cell = set * SOUNDS.size() + sound;
            this->cells[cell] = new CBaseUIStyledButton(
                {}, fmt::format("editor_sample_sets_{}_{}", std::array{"normal", "soft", "drum"}[set], SOUNDS[sound]));
            this->cells[cell]->setClickCallback([this, cell] { this->pick(cell); });
            line->add(this->cells[cell], BoxItem::fixed(CELL_WIDTH));
        }
        this->add(line, BoxItem::fit());
    }

    this->detail = new CBaseUIStyledLabel({}, "editor_sample_sets_file");
    auto *choose = new CBaseUIStyledButton(_("Choose File..."), "editor_sample_sets_choose");
    choose->setClickCallback([this] {
        this->fileDialog = env->openFileWindow(
            [this](const std::vector<std::string> &paths) {
                if(!paths.empty()) this->setFile(paths.front());
            },
            MapFiles::soundFilter(_("Audio files")).c_str(), _("Custom Samples"));
    });
    this->deleteButton = new CBaseUIStyledButton(_("Delete File"), "editor_sample_sets_delete");
    this->deleteButton->setClickCallback([this] { this->deleteFile(); });
    auto *bottom = new CBaseUIBox(Axis::ROW);
    bottom->setGap(style.metrics.gap);
    bottom->add(this->detail, BoxItem::flex())->add(choose, BoxItem::fit())->add(this->deleteButton, BoxItem::fit());
    this->add(bottom, BoxItem::fit());
    this->scan();
}

SampleSets::~SampleSets() { this->hush(); }

u32 SampleSets::open(vec2 pos) {
    this->id = this->popups.open(this, pos);
    return this->id;
}

std::string_view SampleSets::fileFor(std::span<const std::string> files, uSz set, uSz sound, i32 index) {
    const std::string stem = neomod::HitSoundUtils::mapSampleName((u8)set, SOUNDS[sound], (u16)index);
    for(const std::string_view extension : MapFiles::SOUND_EXTENSIONS) {
        for(const std::string &file : files) {
            const std::string_view name{file};
            if(name.size() == stem.size() + 1 + extension.size() && name[stem.size()] == '.' &&
               SString::strcase_equal(name.substr(0, stem.size()), stem) &&
               SString::strcase_equal(name.substr(stem.size() + 1), extension)) {
                return name;
            }
        }
    }
    return {};
}

std::vector<i32> SampleSets::indicesIn(std::span<const std::string> files) {
    // "<set>-<sound>", after which a name has nothing (index 1) or its index from 2 on
    std::array<std::string, SETS * SOUNDS.size()> firsts;
    for(uSz set = 0; set < SETS; set++) {
        for(uSz sound = 0; sound < SOUNDS.size(); sound++) {
            firsts[set * SOUNDS.size() + sound] = neomod::HitSoundUtils::mapSampleName((u8)set, SOUNDS[sound], 1);
        }
    }
    std::vector<i32> indices;
    for(const std::string &file : files) {
        const uSz dot = file.rfind('.');
        if(dot == std::string::npos || !MapFiles::isSound(file)) continue;
        const std::string stem = SString::to_lower(std::string_view{file}.substr(0, dot));
        for(const std::string &first : firsts) {
            if(!stem.starts_with(first)) continue;
            const std::string_view number = std::string_view{stem}.substr(first.size());
            i32 index{1};
            if(!number.empty() && (!Mc::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; }) ||
                                   !Parsing::parse(number, &index) || index < 2)) {
                continue;
            }
            if(!Mc::ranges::contains(indices, index)) indices.push_back(index);
        }
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

void SampleSets::scan() {
    for(std::string &file : this->files) file.clear();
    std::vector<std::string> inFolder;
    if(const DatabaseBeatmap *map = this->state.clock.getMap()) {
        this->read = map->getFolder();
        this->folder = this->read;
        if(!this->folder.ends_with('/')) this->folder.push_back('/');
        inFolder = Environment::getFilesInFolder(this->folder);
    }
    for(uSz set = 0; set < SETS; set++) {
        for(uSz sound = 0; sound < SOUNDS.size(); sound++) {
            this->files[set * SOUNDS.size() + sound] = fileFor(inFolder, set, sound, this->index);
        }
    }
    std::string indices;
    for(const i32 used : indicesIn(inFolder)) indices += fmt::format("{}{}", indices.empty() ? "" : ", ", used);
    this->used->setText(indices.empty() ? std::string{_("This map has no samples of its own.")}
                                        : tformat("This map has samples for: {}", indices));
    this->show();
}

void SampleSets::show() {
    for(uSz cell = 0; cell < this->cells.size(); cell++) {
        const std::string &file = this->files[cell];
        // a file shows as what kind it is
        this->cells[cell]->setText(
            file.empty() ? "-" : SString::to_lower(std::string_view{file}.substr(file.rfind('.') + 1)));
        this->cells[cell]->setChecked(cell == this->picked);
    }
    const std::string &file = this->files[this->picked];
    this->detail->setText(!file.empty() ? file : tformat("{}: not overridden", this->pickedName()));
    this->deleteButton->setEnabled(!file.empty());
}

std::string SampleSets::pickedName() const {
    return neomod::HitSoundUtils::mapSampleName((u8)(this->picked / SOUNDS.size()),
                                                SOUNDS[this->picked % SOUNDS.size()], (u16)this->index);
}

void SampleSets::setIndex(i32 index) {
    this->index = std::clamp(index, 1, MAX_INDEX);
    this->hush();
    this->scan();
}

void SampleSets::pick(uSz cell) {
    this->picked = cell;
    this->hush();
    if(const std::string &file = this->files[cell]; !file.empty()) {
        this->preview.play(this->folder + file);
    } else {
        this->hearSkin();
    }
    this->show();
}

void SampleSets::hearSkin() {
    using namespace neomod::HitSoundUtils;
    const Skin *skin = osu->getSkin();
    if(!skin) return;
    const u8 set = (u8)(this->picked / SOUNDS.size());
    const uSz sound = this->picked % SOUNDS.size();
    if(sound == SLIDERTICK) {
        playSliderTick(*skin, {.volume = 1.f, .set = set, .index = (u16)this->index}, 0.f);
        return;
    }
    // the hit sounds, then the two slider sounds
    const bool slider = sound >= SLIDERSLIDE;
    const ResolvedHitSound resolved{.volume = 1.f,
                                    .set = set,
                                    .slider = (u8)(slider ? 1 : 0),
                                    .hit = (u8)(slider ? sound - SLIDERSLIDE : sound),
                                    .index = (u16)this->index,
                                    .filename = 0};
    std::vector<Set_Slider_Hit> played = play(*skin, {&resolved, 1}, 0.f, 0.f);
    if(slider) {
        this->looping = std::move(played);
        this->loopingUntil = engine->getTime() + LOOP_SECONDS;
    }
}

void SampleSets::hush() {
    this->preview.play({});
    if(this->looping.empty()) return;
    if(const Skin *skin = osu->getSkin()) neomod::HitSoundUtils::stopSliderSounds(*skin, this->looping);
    this->looping.clear();
}

void SampleSets::setFile(const std::string &path) {
    const std::string stem = this->pickedName();
    // the popup may be gone when the copy is done: the file comes in all the same
    if(this->state.copyFirst(
           [&state = this->state, path, stem, changed = this->folderChanged] { put(state, path, stem, changed); })) {
        return;
    }
    if(!put(this->state, path, stem, this->folderChanged)) return;
    this->scan();
    this->pick(this->picked);
}

void SampleSets::deleteFile() {
    const std::string file = this->files[this->picked];
    if(file.empty()) return;
    // the osu!stable folder itself stays as it is: the file can go from the copy, once that's made
    if(this->state.copyFirst([] {})) return;
    this->hush();
    if(!Environment::deleteFile(this->folder + file)) {
        ui->getNotificationOverlay()->addToast(tformat("{} couldn't be deleted.", file), ERROR_TOAST);
        return;
    }
    if(this->folderChanged) this->folderChanged();
    this->scan();
}

void SampleSets::tick() {
    // an osu!stable map goes on in its copy once that's made
    if(const DatabaseBeatmap *map = this->state.clock.getMap(); map && map->getFolder() != this->read) this->scan();
    if(!this->looping.empty() && engine->getTime() >= this->loopingUntil) this->hush();
    CBaseUIBox::tick();
}

void SampleSets::draw() {
    uiStyle().drawPopup(this->getRect());
    CBaseUIBox::draw();
}
