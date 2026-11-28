// Copyright (c) 2026, kiwec, All rights reserved.
#include "CrashReporter.h"

#include "BanchoNetworking.h"
#include "BaseEnvironment.h"
#include "CBaseUIBox.h"
#include "CBaseUICheckbox.h"
#include "CBaseUIDispatch.h"
#include "CBaseUIEventCtx.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "CBaseUIStyledTextbox.h"
#include "ConVar.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Font.h"
#include "Graphics.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "LaunchArgs.h"
#include "Logging.h"
#include "NetworkHandler.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"
#include "Algorithms.h"

#include <cmath>
#include <string>
#include <string_view>

namespace {
struct Link final : CBaseUIButton {
    explicit Link(std::string text) : CBaseUIButton(0, 0, 0, 0, "crashreporter_log", std::move(text)) {}

    void draw() override {
        const UIStyle &style = uiStyle();
        McFont *font = style.font();
        const McRect &rect = this->getRect();
        const float baseline = std::round(rect.getY() + rect.getHeight() / 2.f + font->getHeight() / 2.f);
        g->setColor(this->bMouseInside ? style.palette.accentHovered : style.palette.accent);
        g->pushTransform();
        {
            g->translate(std::round(rect.getX()), baseline);
            g->drawString(font, this->getText());
        }
        g->popTransform();
        if(this->bMouseInside) g->fillRectf(rect.getX(), baseline + 2.f, font->getStringWidth(this->getText()), 1.f);
    }

    vec2 getNaturalSize() override {
        const McFont *font = uiStyle().font();
        return {font->getStringWidth(this->getText()), font->getHeight()};
    }
};
}  // namespace

CrashReporter::CrashReporter() {
    UIStyle &style = uiStyle();
    style.setScale(env->getDPIScale());

    this->comment = new CBaseUIStyledTextbox("crashreporter_comment");
    this->sendCheckbox = new CBaseUICheckbox(0, 0, 0, 0, "crashreporter_send", _("Send crash report"));
    this->sendCheckbox->setDrawFrame(false)->setDrawBackground(false);
    this->sendCheckbox->setFrameColor(style.palette.text)->setTextColor(style.palette.text);
    this->status = new CBaseUIStyledLabel();
    this->quitButton = new CBaseUIStyledButton(_("Quit"), "crashreporter_quit");
    this->quitButton->setClickCallback([this] { this->sendReportThen([] { engine->shutdown(); }); });
    this->restartButton = new CBaseUIStyledButton(_("Restart"), "crashreporter_restart");
    this->restartButton->setChecked(true);
    this->restartButton->setClickCallback([this] { this->sendReportThen([] { engine->restart(); }); });

    auto *buttons = new CBaseUIBox(CBaseUIBox::Axis::ROW);
    buttons->setGap(style.metrics.gap);
    buttons->add(this->status, BoxItem::flex())
        ->add(this->quitButton, BoxItem::fit())
        ->add(this->restartButton, BoxItem::fit());

    const std::string logPath = Logger::filePath();
    auto *log = new Link(Environment::getFileNameFromFilePath(logPath));
    log->setClickCallback([logPath] { env->openInTextEditor(logPath); });
    auto *logLine = new CBaseUIBox(CBaseUIBox::Axis::ROW);
    logLine->add(new CBaseUIStyledLabel("- "), BoxItem::fit())
        ->add(log, BoxItem::fit())
        ->add(new CBaseUIStyledLabel(fmt::format(" {}", _("(may contain your username, file paths, chat messages)"))),
              BoxItem::fit());

    this->root = std::make_unique<CBaseUIBox>(CBaseUIBox::Axis::COLUMN);
    this->root->setPadding(style.metrics.padding * 2.f)->setGap(style.metrics.gap);
    this->root->add(new CBaseUIStyledLabel(_("neomod crashed (Sorry!).")), BoxItem::fit())
        ->add(new CBaseUIStyledLabel(), BoxItem::fixed(style.metrics.padding))
        ->add(new CBaseUIStyledLabel(_("If you want, you can send a crash report to the developers. It includes:")),
              BoxItem::fit())
        ->add(new CBaseUIStyledLabel(_("- the game version and your operating system")), BoxItem::fit())
        ->add(new CBaseUIStyledLabel(_("- where the game crashed")), BoxItem::fit())
        ->add(logLine, BoxItem::fit())
        ->add(new CBaseUIStyledLabel(), BoxItem::fixed(style.metrics.padding))
        ->add(new CBaseUIStyledLabel(_("What were you doing when it crashed? (optional)")), BoxItem::fit())
        ->add(this->comment, BoxItem::fit())
        ->add(this->sendCheckbox, BoxItem::fit(BoxItem::Align::START))
        ->add(new CBaseUIStyledLabel(_("Crash reports are kept for up to 90 days.")), BoxItem::fit())
        ->add(new CBaseUIStyledLabel(), BoxItem::flex())
        ->add(buttons, BoxItem::fit());

    const vec2 size = this->root->getNaturalSize();
    env->setWindowTitle(_("Crash report"));
    env->setWindowResizable(false);
    env->setWindowSize(static_cast<int>(Mc::max(size.x, style.px(520.f))),
                       static_cast<int>(size.y + style.px(style.metrics.padding)));
    env->centerWindow();
    this->root->setSize(engine->getScreenSize());
    this->comment->visit();
}

CrashReporter::~CrashReporter() {
    this->uploadCancel.request_stop();
    uiStyle().setScale(1.f);
}

void CrashReporter::draw() {
    const vec2 size = engine->getScreenSize();
    g->setColor(uiStyle().palette.background);
    g->fillRectf(0.f, 0.f, size.x, size.y);
    this->root->draw();
}

void CrashReporter::update() {
    this->root->tick();
    CBaseUIEventCtx c;
    c.beginHitGroup();
    this->root->updateInput(c);
    CBaseUIDispatch::dispatchEvents(c, CBaseUIDispatch::Root::APP);
    CBaseUIDispatch::offerTextTarget(CBaseUIDispatch::locateFocus(this->root.get()));
}

void CrashReporter::onKeyDown(KeyboardEvent &e) {
    this->root->onKeyDown(e);
    if(!e.isConsumed() && e.getScanCode() == KEY_ESCAPE && this->quitButton->isEnabled()) {
        e.consume();
        this->quitButton->click();
    }
}

void CrashReporter::onKeyUp(KeyboardEvent &e) { this->root->onKeyUp(e); }

void CrashReporter::onResolutionChanged(vec2 newResolution) { this->root->setSize(newResolution); }

void CrashReporter::sendReportThen(void (*leave)()) {
    if(!this->sendCheckbox->isChecked()) return leave();

    this->sendCheckbox->setEnabled(false);
    this->comment->setEnabled(false);
    this->quitButton->setEnabled(false);
    this->restartButton->setEnabled(false);
    this->status->setText(_("Sending the crash report..."));

    const auto file = [](std::string name, const std::string &path) {
        Mc::Net::RequestOptions::MimePart part{
            .filename = Environment::getFileNameFromFilePath(path), .name = std::move(name), .data = {}};
        File(path).readToVector(part.data);
        return part;
    };
    const std::string dir = Mc::LaunchArgs::has_arg(Mc::LaunchArgs::MODE_CRASHREPORT).value_or("");

    const auto field = [](std::string name, std::string_view value) {
        return Mc::Net::RequestOptions::MimePart{
            .filename = {}, .name = std::move(name), .data = {value.begin(), value.end()}};
    };
    Mc::Net::RequestOptions request{
        .mime_parts = {field("version", cv::version.getString()), field("commit", cv::build_commit.getString()),
                       field("os", OS_NAME), field("comment", this->comment->getText()),
                       file("crash", dir + "/crash.txt"), file("log", Logger::filePath())},
        .timeout = 300,
        .connect_timeout = 10,
        .cancel_token = this->uploadCancel.get_token()};

    networkHandler->httpRequestAsync(
        NEOMOD_DOMAIN "/api/v1/crash-reports", std::move(request), [this, leave](const Mc::Net::Response &response) {
            if(response.success) return leave();
            this->status->setText(tformat("Couldn't send the crash report: {}", response.error_msg));
            this->sendCheckbox->setEnabled(true);
            this->comment->setEnabled(true);
            this->quitButton->setEnabled(true);
            this->restartButton->setEnabled(true);
        });
}
