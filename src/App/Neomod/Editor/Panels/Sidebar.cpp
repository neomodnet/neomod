// Copyright (c) 2026, WH, All rights reserved.
#include "Sidebar.h"

#include "CBaseUIDropdown.h"
#include "CBaseUINumberField.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "Commands.h"
#include "ConVar.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmapTypes.h"
#include "EditorState.h"
#include "Engine.h"
#include "Graphics.h"
#include "Keyboard.h"
#include "NotificationOverlay.h"
#include "SampleChooser.h"
#include "Stepper.h"
#include "UI.h"
#include "UIStyle.h"
#include "UniString.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using HitSoundType = neomod::DatabaseBeatmapTypes::HitSoundType;

namespace {
constexpr std::array<u8, 3> HIT_SOUNDS{HitSoundType::WHISTLE, HitSoundType::FINISH, HitSoundType::CLAP};
// how much of a sample file name its button shows
constexpr uSz FILE_CHARACTERS{16};
}  // namespace

// a colour a selected new combo can skip to, framed while it's the one chosen
class Sidebar::Swatch final : public CBaseUIElement {
    NOCOPY_NOMOVE(Swatch)
   public:
    Swatch(EditorState &state, i32 skip)
        : CBaseUIElement(0, 0, 0, 0, fmt::format("editor_colour_skip_{}", skip)), state(state), skip(skip) {}
    ~Swatch() override = default;

    void draw() override {
        if(!this->isVisible()) return;
        const UIStyle &style = uiStyle();
        style.drawControl(this->getRect(),
                          this->bMouseInside ? UIStyle::ControlState::HOVERED : UIStyle::ControlState::NORMAL,
                          this->chosen);
        const f32 inset = style.px(3.f);
        g->setColor(this->colour);
        g->fillRectf(this->getPos().x + inset, this->getPos().y + inset, this->getSize().x - 2.f * inset,
                     this->getSize().y - 2.f * inset);
    }

    Color colour{0};
    bool chosen{false};

   protected:
    void onMouseUpInside(bool left, bool /*right*/) override {
        if(left) this->state.compose.setColourSkip(this->skip);
    }

   private:
    EditorState &state;
    i32 skip;
};

Sidebar::Sidebar(EditorState &state, CBaseUIPopupLayer &popups, std::function<void()> folderChanged)
    : CBaseUIBox(Axis::COLUMN, "editor_sidebar"),
      state(state),
      popups(popups),
      folderChanged(std::move(folderChanged)) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(style.metrics.gap);

    this->newCombo = new CBaseUIStyledButton(_("New Combo"), "editor_new_combo");
    this->newCombo->setClickCallback([this] { this->state.compose.toggleNewCombo(); });
    this->add(this->newCombo, BoxItem::fit());

    const std::array<std::pair<const char *, std::string>, 3> hitSounds{
        {{"editor_whistle", _("Whistle")}, {"editor_finish", _("Finish")}, {"editor_clap", _("Clap")}}};
    for(uSz i = 0; i < hitSounds.size(); i++) {
        this->hitSounds[i] = new CBaseUIStyledButton(hitSounds[i].second, hitSounds[i].first);
        this->hitSounds[i]->setClickCallback(
            [this, hitSound = HIT_SOUNDS[i]] { this->state.compose.toggleHitSound(hitSound); });
        this->add(this->hitSounds[i], BoxItem::fit());
    }

    // as numbered by the file: 0 is auto
    const std::vector<std::string> sets{_("Auto"), _("Normal"), _("Soft"), _("Drum")};
    this->normalSet = new CBaseUIDropdown(popups, sets, "editor_sample_set");
    this->normalSet->setChangeCallback([this](int set) { this->state.compose.setSampleSet(false, set); });
    this->additionSet = new CBaseUIDropdown(popups, sets, "editor_addition_set");
    this->additionSet->setChangeCallback([this](int set) { this->state.compose.setSampleSet(true, set); });
    this->add(new CBaseUIStyledLabel(_("Sample Set")), BoxItem::fit())
        ->add(this->normalSet, BoxItem::fit())
        ->add(new CBaseUIStyledLabel(_("Additions")), BoxItem::fit())
        ->add(this->additionSet, BoxItem::fit());

    // the sample on a hit itself (0: the one its timing point has)
    this->sampleIndex = new CBaseUINumberField("editor_sample_index");
    this->sampleIndex->setRange(0.0, 100.0)->setStep(1.0, 1.0)->setNamedValue(0.0, _("Auto"));
    this->sampleIndex->setChangeCallback(
        [this](f64 index) { explainUnset(this->state.compose.setSampleIndex((i32)index)); });
    this->sampleVolume = new CBaseUINumberField("editor_sample_volume");
    this->sampleVolume->setRange(0.0, 100.0)->setStep(5.0, 1.0)->setSuffix("%")->setNamedValue(0.0, _("Auto"));
    this->sampleVolume->setChangeCallback(
        [this](f64 volume) { explainUnset(this->state.compose.setSampleVolume((i32)volume)); });
    this->sampleFile = new CBaseUIStyledButton({}, "editor_sample_file");
    this->sampleFile->setClickCallback([this] { this->chooseSample(); });
    // each field under its label: the second column is as wide as its label, the first takes the rest
    const auto labelled = [&style](const char *label, CBaseUINumberField *field) {
        field->setNaturalWidth(0.f);
        auto *column = new CBaseUIBox(Axis::COLUMN);
        column->setGap(style.metrics.gap);
        column->add(new CBaseUIStyledLabel(label), BoxItem::fit())->add(field, BoxItem::fit());
        return column;
    };
    auto *own = new CBaseUIBox(Axis::ROW);
    own->setGap(style.metrics.gap);
    own->add(labelled(_("Index"), this->sampleIndex), BoxItem::flex())
        ->add(labelled(_("Volume"), this->sampleVolume), BoxItem::fit());
    this->add(own, BoxItem::fit())->add(this->sampleFile, BoxItem::fit());

    const auto toggle = [](ConVar *setting) {
        if(setting) setting->setValue(!setting->getBool());
    };
    this->gridSnap = new CBaseUIStyledButton(_("Grid Snap"), "editor_grid_snap");
    this->gridSnap->setClickCallback([this, toggle] { toggle(this->state.gridSnap); });
    auto *gridSize = new Stepper(
        "editor_grid_size",
        [this](i32 by) {
            const auto &sizes = Snapping::GRID_SIZES;
            const auto at = Mc::ranges::find(sizes, this->state.gridSize) - sizes.begin();
            this->state.gridSize = sizes[std::clamp<i64>(at + by, 0, (i64)sizes.size() - 1)];
        },
        [this] { return fmt::format("{}", this->state.gridSize); });
    this->distanceSnap = new CBaseUIStyledButton(_("Distance Snap"), "editor_distance_snap");
    this->distanceSnap->setClickCallback([this, toggle] { toggle(this->state.distanceSnap); });
    auto *spacing = new Stepper(
        "editor_spacing",
        [this](i32 by) {
            // Shift: finer
            const f32 step = keyboard->isShiftDown() ? 0.01f : 0.1f;
            this->state.spacing = Snapping::clampSpacing(this->state.spacing + step * (f32)by);
        },
        [this] { return fmt::format("{}x", this->state.spacing); });
    this->add(this->gridSnap, BoxItem::fit())
        ->add(gridSize, BoxItem::fit())
        ->add(this->distanceSnap, BoxItem::fit())
        ->add(spacing, BoxItem::fit());

    // last, so that nothing moves when they show
    auto *swatches = new CBaseUIBox(Axis::ROW);
    swatches->setGap(2.f);
    for(uSz i = 0; i < this->swatches.size(); i++) {
        this->swatches[i] = new Swatch(this->state, (i32)i);
        swatches->add(this->swatches[i], BoxItem::flex());
    }
    this->skips = new CBaseUIBox(Axis::COLUMN, "editor_colour_skips");
    this->skips->setGap(style.metrics.gap);
    this->skips->add(new CBaseUIStyledLabel(_("Combo Colour")), BoxItem::fit())->add(swatches, BoxItem::fixed(18.f));
    this->skips->setVisible(false);
    this->add(this->skips, BoxItem::fit());
}

Sidebar::~Sidebar() = default;

void Sidebar::tick() {
    // a box ticks what it doesn't show too
    if(!this->isVisible()) return;

    const bool shown = this->state.clock.isAttached();
    const Compose &compose = this->state.compose;
    this->newCombo->setEnabled(shown);
    this->newCombo->setChecked(shown && compose.hasNewCombo());
    Commands::hint(*this->newCombo, Commands::Command::NEW_COMBO);
    for(uSz i = 0; i < this->hitSounds.size(); i++) {
        this->hitSounds[i]->setEnabled(shown);
        this->hitSounds[i]->setChecked(shown && compose.hasHitSound(HIT_SOUNDS[i]));
        Commands::hint(*this->hitSounds[i], Commands::Command::HITSOUND, HIT_SOUNDS[i]);
    }
    // as they act now: a held Shift or Alt turns one around
    const Snapping::Settings snap = this->state.snapSettings();
    this->gridSnap->setChecked(snap.grid);
    this->distanceSnap->setChecked(snap.distance);
    Commands::hint(*this->gridSnap, Commands::Command::GRID_SNAP);
    Commands::hint(*this->distanceSnap, Commands::Command::DISTANCE_SNAP);

    const auto choices = shown ? compose.getColourSkips() : std::nullopt;
    this->skips->setVisible(choices.has_value());
    for(uSz i = 0; i < this->swatches.size(); i++) {
        Swatch &swatch = *this->swatches[i];
        swatch.setVisible(choices && i < choices->count);
        if(!choices || i >= choices->count) continue;
        swatch.colour = choices->colours[i];
        swatch.chosen = (i32)i == choices->current;
    }

    this->normalSet->setEnabled(shown);
    this->normalSet->setChosen(shown ? compose.getSampleSet(false).value_or(-1) : -1);
    this->additionSet->setEnabled(shown);
    this->additionSet->setChosen(shown ? compose.getSampleSet(true).value_or(-1) : -1);

    // nothing where the selected hits differ, or sliders alone are selected
    const auto show = [shown](CBaseUINumberField *field, std::optional<i32> value) {
        field->setEnabled(shown);
        if(!field->isFocused() && !field->isAdjusting()) field->setValue(shown && value ? (f64)*value : NAN);
    };
    show(this->sampleIndex, shown ? compose.getSampleIndex() : std::nullopt);
    show(this->sampleVolume, shown ? compose.getSampleVolume() : std::nullopt);
    const auto file = shown ? compose.getSampleFile() : std::optional<std::string_view>{std::string_view{}};
    this->sampleFile->setEnabled(shown);
    this->sampleFile->setChecked(!file || !file->empty());
    // as much of a long name as the button has room for
    std::string name{!file ? std::string_view{_("Several Files")} : file->empty() ? _("Sample File...") : *file};
    if(file && !file->empty()) {
        uSz cut = 0;
        for(uSz shownCharacters = 0; shownCharacters < FILE_CHARACTERS && cut < name.size(); shownCharacters++) {
            cut = UniString::next(name, cut);
        }
        if(cut < name.size()) name.replace(cut, std::string::npos, "..");
    }
    if(this->sampleFile->getText() != name) this->sampleFile->setText(std::move(name));
    CBaseUIBox::tick();
}

void Sidebar::commit() {
    this->sampleIndex->commit();
    this->sampleVolume->commit();
}

void Sidebar::chooseSample() {
    if(this->popups.isOpen(this->chooserId)) {
        this->popups.close(this->chooserId);
        return;
    }
    const auto current = this->state.compose.getSampleFile();
    this->chooser = new SampleChooser(
        this->state, this->popups, current.value_or(std::string_view{}),
        {.take = [this](std::string_view file) { explainUnset(this->state.compose.setSampleFile(file)); },
         .folderChanged = this->folderChanged});
    // left of the sidebar; a press on the button still reaches it, which closes the chooser
    const McRect &rect = this->sampleFile->getRect();
    const f32 gap = uiStyle().px(uiStyle().metrics.gap);
    this->chooserId =
        this->chooser->open({this->getPos().x - this->chooser->getNaturalSize().x - gap, rect.getY()}, rect);
}

bool Sidebar::addSample(const std::string &path) {
    if(!this->popups.isOpen(this->chooserId)) return false;
    this->chooser->addFile(path);
    return true;
}

void Sidebar::explainUnset(bool set) {
    if(set) return;
    ui->getNotificationOverlay()->addNotification(
        _("Sliders take their sample index, volume and file from their timing points."));
}
