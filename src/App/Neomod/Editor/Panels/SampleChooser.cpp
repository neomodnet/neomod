// Copyright (c) 2026, WH, All rights reserved.
#include "SampleChooser.h"

#include "CBaseUIContainer.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "ContainerRanges.h"
#include "EditorState.h"
#include "Environment.h"
#include "MapFiles.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "SString.h"
#include "Skin.h"
#include "UI.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <utility>

using Kind = neomod::BeatmapFile::SectionKind;

namespace {
// units
constexpr f32 NAME_WIDTH{300.f};
constexpr f32 ROWS_SHOWN{10.f};

// whether an object line can name it (the line fields end at commas)
bool writable(std::string_view name) {
    return Mc::ranges::none_of(name, [](char c) { return (unsigned char)c < 32 || c == ','; });
}

void refuse(std::string_view name) {
    ui->getNotificationOverlay()->addNotification(
        tformat("{} can't be used: its name can't be written into a map.", name));
}

// the sound file at `path` as a file in the shown map folder; its name there, none (said) if it can't be one
std::optional<std::string> import(EditorState &state, const std::string &path,
                                  const std::function<void()> &folderChanged) {
    const std::string name = Environment::getFileNameFromFilePath(path);
    if(!MapFiles::isSound(name) || !writable(name)) {
        refuse(name);
        return std::nullopt;
    }
    auto taken = MapFiles::takeIn(MapFiles::folderOf(state.document->getLocation()), path);
    if(!taken) {
        ui->getNotificationOverlay()->addToast(tformat("{} couldn't be copied into the map folder.", name),
                                               ERROR_TOAST);
        return std::nullopt;
    }
    if(folderChanged) folderChanged();
    return taken;
}
}  // namespace

SampleChooser::SampleChooser(EditorState &state, CBaseUIPopupLayer &popups, std::string_view current,
                             Callbacks callbacks)
    : CBaseUIBox(Axis::COLUMN, "editor_sample_chooser"), state(state), popups(popups), callbacks(std::move(callbacks)) {
    const UIStyle &style = uiStyle();
    // a press on its surface stays with it
    this->bClickThroughSelf = false;
    this->setPadding(style.metrics.padding * 2.f);
    this->setGap(style.metrics.gap);

    auto *heading = new CBaseUIStyledLabel(_("Sample File"));
    heading->setDim(true);
    this->rows = new CBaseUIList(*this, {{_("File"), NAME_WIDTH}}, "editor_sample_files");
    this->rows->setSelectionCallback([this] { this->hear(); });
    this->rows->setActivateCallback([this](uSz row) { this->take(this->files[row]); });

    auto *add = new CBaseUIStyledButton(_("Add File..."), "editor_sample_add");
    add->setClickCallback([this] {
        this->fileDialog = env->openFileWindow(
            [this](const std::vector<std::string> &paths) {
                if(!paths.empty()) this->addFile(paths.front());
            },
            MapFiles::soundFilter(_("Audio files")).c_str(), _("Sample File"));
    });
    auto *none = new CBaseUIStyledButton(_("No File"), "editor_sample_none");
    none->setClickCallback([this] { this->take({}); });
    this->use = new CBaseUIStyledButton(_("Use"), "editor_sample_use");
    this->use->setClickCallback([this] {
        if(const std::string *file = this->picked()) this->take(*file);
    });
    auto *buttons = new CBaseUIBox(Axis::ROW);
    buttons->setGap(style.metrics.gap);
    buttons->add(add, BoxItem::fit())
        ->add(new CBaseUIContainer(), BoxItem::flex())
        ->add(none, BoxItem::fit())
        ->add(this->use, BoxItem::fit());

    this->add(heading, BoxItem::fit())
        ->add(this->rows, BoxItem::fixed((ROWS_SHOWN + 1.f) * style.metrics.rowHeight))
        ->add(buttons, BoxItem::fit());
    this->list(current);
}

SampleChooser::~SampleChooser() = default;

u32 SampleChooser::open(vec2 pos, const McRect &passThrough) {
    this->id = this->popups.open(this, pos, passThrough);
    // the arrow keys go through the files
    this->rows->requestFocus();
    return this->id;
}

void SampleChooser::tick() {
    this->use->setEnabled(this->picked() != nullptr);
    CBaseUIBox::tick();
}

void SampleChooser::draw() {
    uiStyle().drawPopup(this->getRect());
    CBaseUIBox::draw();
}

void SampleChooser::list(std::string_view picked) {
    this->files.clear();
    this->rows->setSelection({});
    if(!this->state.clock.isAttached()) return;
    this->folder = MapFiles::folderOf(this->state.document->getLocation());

    const std::string_view song =
        this->state.document->getFile().getValue(Kind::GENERAL, "AudioFilename").value_or(std::string_view{});
    for(std::string &file : Environment::getFilesInFolder(this->folder)) {
        if(MapFiles::isSound(file) && !SString::strcase_equal(file, song)) this->files.push_back(std::move(file));
    }
    std::sort(this->files.begin(), this->files.end(), [](const std::string &a, const std::string &b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower((unsigned char)x) < std::tolower((unsigned char)y);
        });
    });

    // named with or without its extension
    const auto at = Mc::ranges::find_if(this->files, [&](const std::string &file) {
        return SString::strcase_equal(file, picked) ||
               SString::strcase_equal(std::string_view{file}.substr(0, file.rfind('.')), picked);
    });
    if(picked.empty() || at == this->files.end()) return;
    const u64 row = (u64)(at - this->files.begin());
    this->rows->setSelection({&row, 1});
    this->rows->scrollTo((uSz)row);
}

const std::string *SampleChooser::picked() const {
    const auto &selection = this->rows->getSelection();
    if(selection.size() != 1 || *selection.begin() >= this->files.size()) return nullptr;
    return &this->files[(uSz)*selection.begin()];
}

void SampleChooser::hear() {
    const std::string *file = this->picked();
    this->preview.play(file ? this->folder + *file : std::string{});
}

void SampleChooser::take(std::string_view file) {
    if(!writable(file)) {
        refuse(file);
        return;
    }
    // the skin can't play a file its folder got after the skin read it yet
    if(const Skin *skin = osu->getSkin();
       !file.empty() && skin && skin->hasBeatmapOverride() && skin->getBeatmapFile(file).empty()) {
        if(this->callbacks.folderChanged) this->callbacks.folderChanged();
    }
    this->callbacks.take(file);
    this->popups.close(this->id);
}

void SampleChooser::addFile(const std::string &path) {
    // the popup may be gone when the copy is done: the file comes in all the same, for the next one to list
    if(this->state.copyFirst(
           [&state = this->state, path, changed = this->callbacks.folderChanged] { import(state, path, changed); })) {
        return;
    }
    if(const auto name = import(this->state, path, this->callbacks.folderChanged)) {
        this->list(*name);
        this->hear();
    }
}
