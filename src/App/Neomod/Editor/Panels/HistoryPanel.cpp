// Copyright (c) 2026, WH, All rights reserved.
#include "HistoryPanel.h"

#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "ContainerRanges.h"
#include "Document.h"
#include "EditorState.h"
#include "Timing.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <array>
#include <ctime>
#include <optional>
#include <utility>

using Changes = EditorHistory::Changes;
using Version = EditorHistory::Version;

namespace {
// "+3 -1 objects, +1 timing points, 2 other lines" (the parts that are there; "nothing" without any)
std::string describe(const Changes &c) {
    std::string out;
    const auto part = [&](std::string text) {
        if(!out.empty()) out += ", ";
        out += text;
    };
    const auto counts = [](u32 added, u32 removed) {
        return added && removed ? fmt::format("+{} -{}", added, removed)
               : added          ? fmt::format("+{}", added)
                                : fmt::format("-{}", removed);
    };
    if(c.objectsAdded || c.objectsRemoved) part(tformat("{} objects", counts(c.objectsAdded, c.objectsRemoved)));
    if(c.timingAdded || c.timingRemoved) part(tformat("{} timing points", counts(c.timingAdded, c.timingRemoved)));
    if(c.otherLines) part(tformat("{} other lines", c.otherLines));
    return out.empty() ? std::string{_("nothing")} : out;
}
}  // namespace

HistoryPanel::HistoryPanel(EditorState &state) : CBaseUIBox(Axis::COLUMN, "editor_history"), state(state) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(style.metrics.gap);

    this->detail = new CBaseUIStyledLabel({}, "editor_history_detail");
    this->restoreButton = new CBaseUIStyledButton(_("Restore This Version"), "editor_history_restore");
    this->restoreButton->setClickCallback([this] { this->restore(); });
    this->rows = new CBaseUIList(*this, {{_("Saved At"), 200.f}, {_("Objects"), 90.f}, {_("Changes"), 0.f}},
                                 "editor_history_versions");
    this->rows->setSelectionCallback([this] { this->compared.reset(); });
    this->rows->setActivateCallback([this](uSz) { this->restore(); });
    auto *top = new CBaseUIBox(Axis::ROW);
    top->setGap(style.metrics.gap);
    top->add(this->detail, BoxItem::flex())->add(this->restoreButton, BoxItem::fit());
    this->add(top, BoxItem::fit())->add(this->rows, BoxItem::flex());
}

HistoryPanel::~HistoryPanel() = default;

void HistoryPanel::tick() {
    const Document *document = this->state.document.get();
    if(this->isVisible() && document) {
        // a save gives the file new content, and the map another version
        if(this->listed != document->getMD5()) {
            this->listed = document->getMD5();
            this->versions = EditorHistory::versionsOf(document->getLocation(), document->getMD5());
            this->rows->setSelection({});
            this->compared.reset();
        }
        const Version *version = this->selected();
        this->restoreButton->setEnabled(version != nullptr);
        if(this->compared != document->getRevision()) {
            this->compared = document->getRevision();
            const auto content = version ? EditorHistory::read(*version) : std::nullopt;
            this->detail->setText(this->versions.empty() ? std::string{}
                                  : !version ? std::string{_("Select a version to compare it with the map as it is.")}
                                  : !content
                                      ? std::string{_("This version can't be read anymore.")}
                                      : tformat("From this version to the map now: {}",
                                                describe(EditorHistory::compare(*content, document->getBytes()))));
        }
    }
    CBaseUIBox::tick();
}

void HistoryPanel::clear() {
    this->versions.clear();
    this->listed.reset();
    this->compared.reset();
    this->rows->setSelection({});
}

uSz HistoryPanel::getRowCount() const { return this->versions.size(); }

u64 HistoryPanel::getRowKey(uSz row) const { return (u64)this->versions[row].time; }

std::string HistoryPanel::getCellText(uSz row, uSz column) const {
    const Version &version = this->versions[row];
    switch(column) {
        case 0: {
            const std::time_t seconds = (std::time_t)(version.time / 1000);
            std::tm tm{};
            localtime_x(&seconds, &tm);
            std::array<char, 32> text{};
            return {text.data(), std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M:%S", &tm)};
        }
        case 1:
            return fmt::format("{}", version.objects);
        default:
            // a found one is what the file held when a save overwrote it. the oldest has no version before it
            return version.saved || row + 1 == this->versions.size()
                       ? describe(version.changes)
                       : tformat("Changed outside the editor: {}", describe(version.changes));
    }
}

CBaseUIList::RowLook HistoryPanel::getRowLook(uSz row) const {
    // the version the file holds
    return {.bar = std::nullopt, .marked = this->listed && this->versions[row].md5 == *this->listed};
}

const Version *HistoryPanel::selected() const {
    const auto &selection = this->rows->getSelection();
    if(selection.size() != 1) return nullptr;
    const auto it = Mc::ranges::find(this->versions, (i64)*selection.begin(), &Version::time);
    return it != this->versions.end() ? &*it : nullptr;
}

void HistoryPanel::restore() {
    const Version *version = this->selected();
    if(!version || !this->state.document) return;
    if(auto content = EditorHistory::read(*version)) this->state.restore(std::move(*content));
}
