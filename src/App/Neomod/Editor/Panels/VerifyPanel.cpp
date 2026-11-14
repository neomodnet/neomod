// Copyright (c) 2026, WH, All rights reserved.
#include "VerifyPanel.h"

#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "Color.h"
#include "Database.h"
#include "Environment.h"
#include "TimeDisplay.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <array>
#include <utility>

using Issue = Verify::Issue;
using Kind = Verify::Kind;
using Severity = Verify::Severity;
using Tab = EditorState::Tab;

namespace {
constexpr Color PROBLEM_COLOR{argb(255, 220, 60, 60)};
constexpr Color WARNING_COLOR{argb(255, 230, 190, 60)};
constexpr Color MINOR_COLOR{argb(255, 120, 120, 132)};

// the tab an issue is fixed on
Tab tabOf(Kind kind) {
    switch(kind) {
        case Kind::TIMING_OVERLAP:
            return Tab::TIMING;
        case Kind::NO_PREVIEW:
        case Kind::NO_BACKGROUND:
        case Kind::MISSING_BACKGROUND:
        case Kind::MISSING_AUDIO:
        case Kind::SETTING_DECIMALS:
        case Kind::SETTING_RANGE:
            return Tab::SETUP;
        default:
            return Tab::COMPOSE;
    }
}

std::string describe(const Issue &issue) {
    const auto setting = [&]() -> std::string_view {
        switch(issue.setting) {
            case Verify::Setting::HP:
                return _("HP Drain Rate");
            case Verify::Setting::CS:
                return _("Circle Size");
            case Verify::Setting::OD:
                return _("Overall Difficulty");
            case Verify::Setting::AR:
                return _("Approach Rate");
        }
        return {};
    };
    switch(issue.kind) {
        case Kind::UNSNAPPED:
            return tformat("This object starts {} ms off the nearest tick.", fmt::format("{:.1f}", issue.value));
        case Kind::UNSNAPPED_REPEAT:
            return tformat("This slider has a repeat {} ms off the nearest tick.", fmt::format("{:.1f}", issue.value));
        case Kind::UNSNAPPED_END:
            return tformat("This object ends {} ms off the nearest tick.", fmt::format("{:.1f}", issue.value));
        case Kind::CONCURRENT:
            return std::string{_("These objects are at the same time.")};
        case Kind::ALMOST_CONCURRENT:
            return std::string{_("These objects are less than 10 ms apart.")};
        case Kind::OFFSCREEN:
            return std::string{_("This object leaves the screen at a 4:3 aspect ratio.")};
        case Kind::SHORT_SPINNER:
            return tformat("This spinner lasts only {} ms.", (i32)issue.value);
        case Kind::SPINNER_WITHOUT_NEW_COMBO:
            return std::string{_("This spinner doesn't start a new combo.")};
        case Kind::QUIET:
            return tformat("This object is hit at {}% volume.", (i32)issue.value);
        case Kind::TIMING_OVERLAP:
            return std::string{_("Two timing points of the same kind are at this time.")};
        case Kind::NO_PREVIEW:
            return std::string{_("The map has no preview point.")};
        case Kind::NO_BACKGROUND:
            return std::string{_("The map has no background image.")};
        case Kind::MISSING_BACKGROUND:
            return std::string{_("The background image file is missing.")};
        case Kind::MISSING_AUDIO:
            return std::string{_("The audio file is missing.")};
        // as the f32 the game reads it as: 8.33, not 8.329999923706055
        case Kind::SETTING_DECIMALS:
            return tformat("{} is {}, with more than one decimal place.", setting(), (f32)issue.value);
        case Kind::SETTING_RANGE:
            return tformat("{} is {}, outside of 0 to 10.", setting(), (f32)issue.value);
        case Kind::SHORT_DRAIN:
            return tformat("The map has less than 30 seconds of drain time ({} s).", (i32)issue.value);
    }
    return {};
}
}  // namespace

VerifyPanel::VerifyPanel(EditorState &state, std::function<void(Tab)> show)
    : CBaseUIBox(Axis::COLUMN, "editor_verify"), state(state), show(std::move(show)) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(style.metrics.gap);

    this->summary = new CBaseUIStyledLabel({}, "editor_verify_summary");
    this->rows = new CBaseUIList(*this, {{{}, 100.f}, {_("Time"), 110.f}, {_("Issue"), 0.f}}, "editor_verify_issues");
    this->rows->setSelectionCallback([this] {
        const auto &selection = this->rows->getSelection();
        if(selection.size() == 1 && *selection.begin() < this->issues.size()) {
            this->goTo(this->issues[*selection.begin()]);
        }
    });
    this->rows->setActivateCallback([this](uSz row) {
        if(row >= this->issues.size()) return;
        // by value: the tab change may look for them again
        const Issue issue = this->issues[row];
        this->goTo(issue);
        this->show(tabOf(issue.kind));
    });
    this->minor = new CBaseUIStyledButton(_("Show Minor Issues"), "editor_verify_minor");
    this->minor->setClickCallback([this] { this->list(!this->minor->isChecked()); });
    auto *top = new CBaseUIBox(Axis::ROW);
    top->setGap(style.metrics.gap);
    top->add(this->summary, BoxItem::flex())->add(this->minor, BoxItem::fit());
    this->add(top, BoxItem::fit())->add(this->rows, BoxItem::flex());
}

VerifyPanel::~VerifyPanel() = default;

void VerifyPanel::tick() {
    const Document *document = this->state.document.get();
    if(this->isVisible() && document && this->checked != document->getRevision()) {
        this->checked = document->getRevision();
        this->found = Verify::check(*document, *this->state.objects,
                                    Environment::getFolderFromFilePath(db->pathOf(document->getLocation())));
        this->list(this->minor->isChecked());

        std::array<uSz, 3> counts{};
        for(const Issue &issue : this->found) counts[(uSz)issue.severity]++;
        this->summary->setText(
            this->found.empty() ? std::string{_("No issues found.")}
                                : tformat("Problems: {}   Warnings: {}   Minor: {}", counts[0], counts[1], counts[2]));
    }
    CBaseUIBox::tick();
}

void VerifyPanel::clear() {
    this->found.clear();
    this->checked.reset();
    this->list(this->minor->isChecked());
}

void VerifyPanel::list(bool minor) {
    this->minor->setChecked(minor);
    this->issues.clear();
    for(const Issue &issue : this->found) {
        if(minor || issue.severity != Severity::MINOR) this->issues.push_back(issue);
    }
    this->rows->setSelection({});
}

uSz VerifyPanel::getRowCount() const { return this->issues.size(); }

u64 VerifyPanel::getRowKey(uSz row) const { return row; }

std::string VerifyPanel::getCellText(uSz row, uSz column) const {
    const Issue &issue = this->issues[row];
    switch(column) {
        case 0:
            switch(issue.severity) {
                case Severity::PROBLEM:
                    return std::string{_("Problem")};
                case Severity::WARNING:
                    return std::string{_("Warning")};
                case Severity::MINOR:
                    return std::string{_("Minor")};
            }
            return {};
        case 1:
            return issue.time ? TimeDisplay::format(*issue.time) : std::string{};
        default:
            return describe(issue);
    }
}

CBaseUIList::RowLook VerifyPanel::getRowLook(uSz row) const {
    switch(this->issues[row].severity) {
        case Severity::PROBLEM:
            return {.bar = PROBLEM_COLOR};
        case Severity::WARNING:
            return {.bar = WARNING_COLOR};
        case Severity::MINOR:
            return {.bar = MINOR_COLOR};
    }
    return {};
}

void VerifyPanel::goTo(const Issue &issue) {
    if(!issue.time || !this->state.clock.isAttached()) return;
    this->state.clock.pause();
    this->state.clock.seek(*issue.time);
    if(issue.kind == Kind::TIMING_OVERLAP) return;
    std::vector<Document::Id> lines;
    for(const Document::Id line : issue.lines) {
        if(line != 0) lines.push_back(line);
    }
    this->state.compose.select(std::move(lines));
}
