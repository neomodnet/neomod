// Copyright (c) 2026, WH, All rights reserved.
#include "Toolbox.h"

#include "CBaseUIStyledButton.h"
#include "Commands.h"
#include "EditorState.h"
#include "UIStyle.h"
#include "i18n.h"

#include <array>
#include <string>
#include <utility>

using Tool = Compose::Tool;

Toolbox::Toolbox(EditorState &state) : CBaseUIBox(Axis::COLUMN, "editor_toolbox"), state(state) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(style.metrics.gap);

    const std::array<std::pair<const char *, std::string>, 4> tools{{{"editor_tool_select", _("Select")},
                                                                     {"editor_tool_circle", _("Circle")},
                                                                     {"editor_tool_slider", _("Slider")},
                                                                     {"editor_tool_spinner", _("Spinner")}}};
    for(uSz i = 0; i < tools.size(); i++) {
        auto *button = new CBaseUIStyledButton(tools[i].second, tools[i].first);
        button->setClickCallback([this, tool = (Tool)i] { this->state.compose.setTool(tool); });
        this->buttons[i] = button;
        this->add(button, BoxItem::fit());
    }
}

Toolbox::~Toolbox() = default;

void Toolbox::tick() {
    const bool shown = this->state.clock.isAttached();
    for(uSz i = 0; i < this->buttons.size(); i++) {
        this->buttons[i]->setEnabled(shown);
        this->buttons[i]->setChecked(shown && this->state.compose.getTool() == (Tool)i);
        Commands::hint(*this->buttons[i], Commands::Command::TOOL, (i32)i);
    }
    CBaseUIBox::tick();
}
