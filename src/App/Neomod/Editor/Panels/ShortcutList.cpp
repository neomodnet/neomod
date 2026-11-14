// Copyright (c) 2026, WH, All rights reserved.
#include "ShortcutList.h"

#include "CBaseUIStyledLabel.h"
#include "Commands.h"
#include "ContainerRanges.h"
#include "UIStyle.h"
#include "i18n.h"

#include <utility>

namespace {
// units
constexpr f32 LIST_HEIGHT{460.f};
}  // namespace

ShortcutList::ShortcutList() : CBaseUIBox(Axis::COLUMN, "editor_shortcuts") {
    const UIStyle &style = uiStyle();
    // a press on its surface stays with it
    this->bClickThroughSelf = false;
    this->setPadding(style.metrics.padding * 2.f);
    this->setGap(style.metrics.gap);

    // a command with several keys is listed once, where its first key is
    std::vector<std::pair<Commands::Command, i32>> listed;
    for(const Commands::Binding &binding : Commands::bindings()) {
        if(!Mc::ranges::contains(listed, std::pair{binding.command, binding.value})) {
            listed.emplace_back(binding.command, binding.value);
        }
    }
    Mc::ranges::stable_sort(listed, {}, [](const auto &command) { return Commands::group(command.first); });
    for(uSz i = 0; i < listed.size(); i++) {
        const auto [command, value] = listed[i];
        const Commands::Group group = Commands::group(command);
        const bool first = i == 0 || Commands::group(listed[i - 1].first) != group;
        this->rows.push_back(
            {first ? Commands::groupName(group) : "", Commands::label(command, value), Commands::keys(command, value)});
    }

    auto *heading = new CBaseUIStyledLabel(_("Keyboard Shortcuts"));
    heading->setDim(true);
    auto *list =
        new CBaseUIList(*this, {{{}, 110.f}, {_("Command"), 280.f}, {_("Keys"), 200.f}}, "editor_shortcut_rows");
    this->add(heading, BoxItem::fit())->add(list, BoxItem::fixed(LIST_HEIGHT));
}

ShortcutList::~ShortcutList() = default;

void ShortcutList::draw() {
    uiStyle().drawPopup(this->getRect());
    CBaseUIBox::draw();
}

uSz ShortcutList::getRowCount() const { return this->rows.size(); }

u64 ShortcutList::getRowKey(uSz row) const { return row; }

std::string ShortcutList::getCellText(uSz row, uSz column) const { return this->rows[row][column]; }
