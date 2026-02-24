// Copyright (c) 2020, PG, All rights reserved.
#include "VisualProfiler.h"

#include "AnimationHandler.h"
#include "ConVar.h"
#include "ConVarHandler.h"
#include "Engine.h"
#include "Environment.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Profiler.h"
#include "AsyncPool.h"
#include "ResourceManager.h"
#include "Resource.h"
#include "RuntimePlatform.h"
#include "SoundEngine.h"
#include "Font.h"
#include "VertexArrayObject.h"
#include "SysMon.h"

#include <cstring>

namespace cv {
static ConVar vprof_sysinfo_refresh_interval("vprof_sysinfo_refresh_interval", 0.5f, CLIENT);
}

struct VProfGatherer final {
    VProfGatherer() : worker(this) {}
    struct InfoBundle {
        SysMon::MemoryInfo mem;
        SysMon::CpuInfo cpu;
    };

    [[nodiscard]] const InfoBundle &getLatest() const { return this->lastInfo; }

    void update() {
        if(const f64 now = engine->getTime();
           this->lastCheckTime + cv::vprof_sysinfo_refresh_interval.getFloat() < now) {
            this->lastCheckTime = now;

            if(!this->waiting) {
                resourceManager->reloadResource(&this->worker, true);
                this->waiting = true;
            }
        }
        return;
    }

   private:
    class Dispatchee : public Resource {
        NOCOPY_NOMOVE(Dispatchee)
       public:
        Dispatchee(VProfGatherer *parent) : Resource(APPDEFINED), parent(parent) {}
        ~Dispatchee() override { this->destroy(); }
        void init() override {
            parent->waiting = false;
            parent->lastInfo = this->scratchInfo;
            // So we don't spam updates unnecessarily fast if it can't keep up for some reason.
            parent->lastCheckTime = engine->getTime();
        }
        void initAsync() override {
            if(!this->isInterrupted()) {
                SysMon::getCpuInfo(this->scratchInfo.cpu);
                SysMon::getMemoryInfo(this->scratchInfo.mem);
            }
            this->setAsyncReady(true);
            this->setReady(true);
        }
        void destroy() override {}

       private:
        friend VProfGatherer;
        InfoBundle scratchInfo{};
        VProfGatherer *parent;
    };

    Dispatchee worker;
    InfoBundle lastInfo{};
    f64 lastCheckTime{0.};
    bool waiting{false};
};

VisualProfiler *vprof = nullptr;

VisualProfiler::VisualProfiler() : CBaseUIElement(0, 0, 0, 0, "") {
    vprof = this;
    this->infoGatherer = std::make_unique<VProfGatherer>();

    this->textLines.reserve(128);
    this->engineTextLines.reserve(128);
    this->appTextLines.reserve(128);

    this->spike.node.depth = -1;
    this->spike.node.node = nullptr;
    this->spike.timeLastFrame = 0.0;

    this->iPrevVaoWidth = 0;
    this->iPrevVaoHeight = 0;
    this->iPrevVaoGroups = 0;
    this->fPrevVaoMaxRange = 0.0f;
    this->fPrevVaoAlpha = -1.0f;

    this->iCurLinePos = 0;

    this->iDrawGroupID = -1;
    this->iDrawSwapBuffersGroupID = -1;

    this->spikeIDCounter = 0;

    this->setProfile(&g_profCurrentProfile);  // by default we look at the standard full engine-wide profile

    this->font = engine->getDefaultFont();
    this->fontConsole = resourceManager->getFont("FONT_CONSOLE");
    this->lineVao = resourceManager->createVertexArrayObject(DrawPrimitive::LINES, DrawUsageType::DYNAMIC, true);

    this->bScheduledForceRebuildLineVao = false;
    this->bRequiresAltShiftKeysToFreeze = false;
}

VisualProfiler::~VisualProfiler() { vprof = nullptr; }

void VisualProfiler::draw() {
    VPROF_BUDGET("VisualProfiler::draw", VPROF_BUDGETGROUP_DRAW);
    if(!cv::vprof.getBool() || !this->bVisible) return;

    // draw miscellaneous debug infos
    const auto displayMode = cv::vprof_display_mode.getVal<INFO_BLADE_DISPLAY_MODE>();
    if(displayMode != INFO_BLADE_DISPLAY_MODE::DEFAULT) {
        McFont *textFont = this->font;
        float textScale = 1.0f;
        {
            switch(displayMode) {
                case INFO_BLADE_DISPLAY_MODE::GPU_INFO: {
                    const int vramTotalMB = g->getVRAMTotal() / 1024;
                    const int vramRemainingMB = g->getVRAMRemaining() / 1024;

                    UString vendor = g->getVendor();
                    UString model = g->getModel();
                    UString version = g->getVersion();

                    addTextLine(fmt::format("GPU Vendor: {:s}"_cf, vendor), textFont, this->textLines);
                    addTextLine(fmt::format("Model: {:s}"_cf, model), textFont, this->textLines);
                    addTextLine(fmt::format("Version: {:s}"_cf, version), textFont, this->textLines);
                    addTextLine(
                        fmt::format("Resolution: {:d} x {:d}"_cf, (int)g->getResolution().x, (int)g->getResolution().y),
                        textFont, this->textLines);
                    addTextLine(fmt::format("NativeRes: {:d} x {:d}"_cf, (int)env->getNativeScreenSize().x,
                                            (int)env->getNativeScreenSize().y),
                                textFont, this->textLines);
                    addTextLine(fmt::format("Env Pixel Density: {:f}"_cf, env->getPixelDensity()), textFont,
                                this->textLines);
                    addTextLine(fmt::format("Env DPI Scale: {:f}"_cf, env->getDPIScale()), textFont, this->textLines);
                    addTextLine(fmt::format("Env DPI: {:d}"_cf, (int)env->getDPI()), textFont, this->textLines);
                    addTextLine(fmt::format("Renderer: {:s}"_cf, g->getName()), textFont, this->textLines);  //
                    addTextLine(fmt::format("VRAM: {:d} MB / {:d} MB"_cf, vramRemainingMB, vramTotalMB), textFont,
                                this->textLines);
                } break;

                case INFO_BLADE_DISPLAY_MODE::CPU_RAM_INFO: {
                    textFont = this->fontConsole;
                    textScale = std::round(env->getDPIScale() + 0.255f);

                    const auto &[minfo, cinfo] = this->infoGatherer->getLatest();

                    addTextLine(US_("CPU info:"), {}, textFont, this->textLines);

                    addTextLine(US_("Threads:"), fmt::format("{:d}"_cf, cinfo.threadCount), textFont, this->textLines);

                    addTextLine(US_("CPU Usage:"), fmt::format("{:.2f}%"_cf, cinfo.cpuUsage), textFont,
                                this->textLines);
                    addTextLine(US_("CPU Time (usr):"), fmt::format("{:.2f}s"_cf, cinfo.userTime), textFont,
                                this->textLines);
                    addTextLine(US_("CPU Time (krnl):"), fmt::format("{:.2f}s"_cf, cinfo.kernelTime), textFont,
                                this->textLines);

                    addTextLine(US_("CTXT (Vol.):"), fmt::format("{:d}"_cf, cinfo.voluntaryCtxSwitches), textFont,
                                this->textLines);
                    addTextLine(US_("CTXT (Invol.):"), fmt::format("{:d}"_cf, cinfo.involuntaryCtxSwitches), textFont,
                                this->textLines);

                    addTextLine({}, textFont, this->textLines);

                    addTextLine(US_("RAM info:"), {}, textFont, this->textLines);
                    addTextLine(US_("Total Physical:"), fmt::format("{:d}MB"_cf, minfo.totalPhysical / (1024UL * 1024)),
                                textFont, this->textLines);
                    addTextLine(US_("Avail Physical:"), fmt::format("{:d}MB"_cf, minfo.availPhysical / (1024UL * 1024)),
                                textFont, this->textLines);
                    addTextLine(US_("Total Virtual:"), fmt::format("{:d}MB"_cf, minfo.totalVirtual / (1024UL * 1024)),
                                textFont, this->textLines);
                    addTextLine(US_("Usage (RSS):"), fmt::format("{:d}MB"_cf, minfo.currentRSS / (1024UL * 1024)),
                                textFont, this->textLines);
                    addTextLine(US_("Peak RSS:"), fmt::format("{:d}MB"_cf, minfo.peakRSS / (1024UL * 1024)), textFont,
                                this->textLines);

                    addTextLine(US_("Virtual:"), fmt::format("{:d}MB"_cf, minfo.virtualSize / (1024UL * 1024)),
                                textFont, this->textLines);
                    addTextLine(US_("Private:"), fmt::format("{:d}MB"_cf, minfo.privateBytes / (1024UL * 1024)),
                                textFont, this->textLines);
                    addTextLine(US_("Shared:"), fmt::format("{:d}MB"_cf, minfo.sharedBytes / (1024UL * 1024)), textFont,
                                this->textLines);

                    addTextLine(US_("Page Faults:"), fmt::format("{:d}"_cf, minfo.pageFaults), textFont,
                                this->textLines);
                } break;

                case INFO_BLADE_DISPLAY_MODE::ENGINE_INFO: {
                    textFont = this->fontConsole;
                    textScale = std::round(env->getDPIScale() + 0.255f);

                    const double time = engine->getTime();
                    const vec2 envMousePos = env->getMousePos();

                    addTextLine(fmt::format("Platform: {:s}"_cf, RuntimePlatform::current_string()), textFont,
                                this->textLines);
                    addTextLine(fmt::format("ConVars: {:d}"_cf, cvars().getNumConVars()), textFont, this->textLines);
                    addTextLine(fmt::format("Monitor: [{:d}] of {:d}"_cf, env->getMonitor(), env->getMonitors().size()),
                                textFont, this->textLines);
                    addTextLine(fmt::format("Env Mouse Pos: {:d} x {:d}"_cf, (int)envMousePos.x, (int)envMousePos.y),
                                textFont, this->textLines);
                    addTextLine(fmt::format("Mouse Input Grabbed: {}"_cf, env->isMouseInputGrabbed()), textFont,
                                this->textLines);
                    addTextLine(fmt::format("Raw Mouse Input: {}"_cf, env->isOSMouseInputRaw()), textFont,
                                this->textLines);
                    addTextLine(fmt::format("Keyboard Input Grabbed: {}"_cf, env->isKeyboardInputGrabbed()), textFont,
                                this->textLines);
                    if constexpr(Env::cfg(OS::WINDOWS)) {
                        addTextLine(fmt::format("Raw Keyboard Input: {}"_cf, env->isOSKeyboardInputRaw()), textFont,
                                    this->textLines);
                    }
                    addTextLine(fmt::format("Sound Device: {:s}"_cf, soundEngine->getOutputDeviceName()), textFont,
                                this->textLines);
                    addTextLine(fmt::format("Sound Volume: {:f}"_cf, soundEngine->getVolume()), textFont,
                                this->textLines);
                    addTextLine(
                        fmt::format("Pool: {:d} threads, {:d} pending"_cf, Async::pool().thread_count(),
                                    Async::pool().pending_count()),
                        textFont, this->textLines);
                    addTextLine(
                        fmt::format("RM InFlight: {:d}, DestroyQ: {:d}"_cf, resourceManager->getNumInFlight(),
                                    resourceManager->getNumAsyncDestroyQueue()),
                        textFont, this->textLines);
                    addTextLine(fmt::format("RM Named Resources: {:d}"_cf, resourceManager->getResources().size()),
                                textFont, this->textLines);
                    addTextLine(fmt::format("Animations: {:d}"_cf, anim::getNumActiveAnimations()), textFont,
                                this->textLines);
                    addTextLine(fmt::format("Frame: {:d}"_cf, engine->getFrameCount()), textFont, this->textLines);
                    addTextLine(fmt::format("Time: {:f}"_cf, time), textFont, this->textLines);

                    for(const auto &engineTextLine : this->engineTextLines) {
                        addTextLine(engineTextLine, textFont, this->textLines);
                    }
                } break;

                case INFO_BLADE_DISPLAY_MODE::APP_INFO: {
                    textFont = this->fontConsole;
                    textScale = std::round(env->getDPIScale() + 0.255f);

                    for(const auto &appTextLine : this->appTextLines) {
                        addTextLine(appTextLine, textFont, this->textLines);
                    }

                    if(this->appTextLines.size() < 1) addTextLine("(Empty)", textFont, this->textLines);
                } break;
                default:
                    break;
            }
        }

        if(this->textLines.size() > 0) {
            const Color textColor = 0xffffffff;
            const int margin = cv::vprof_graph_margin.getFloat() * env->getDPIScale();
            const int marginBox = 6 * env->getDPIScale();

            int largestLineWidth = 0;
            for(const TEXT_LINE &textLine : this->textLines) {
                if(textLine.width() > largestLineWidth) largestLineWidth = textLine.width();
            }
            largestLineWidth *= textScale;

            const int boxX = -margin + (engine->getScreenWidth() - largestLineWidth) - marginBox;
            const int boxY = margin - marginBox;
            const int boxWidth = largestLineWidth + 2 * marginBox;
            const int boxHeight = marginBox * 2 + textFont->getHeight() * textScale +
                                  (textFont->getHeight() * textScale * 1.5f) * (this->textLines.size() - 1);

            // draw background
            g->setColor(0xaa000000);
            g->fillRect(boxX - 1, boxY - 1, boxWidth + 1, boxHeight + 1);
            g->setColor(0xff777777);
            g->drawRect(boxX - 1, boxY - 1, boxWidth + 1, boxHeight + 1);

            // draw text
            g->pushTransform();
            {
                g->scale(textScale, textScale);
                g->translate(-margin, (int)(textFont->getHeight() * textScale + margin));

                for(size_t i = 0; i < this->textLines.size(); i++) {
                    if(i > 0) g->translate(0, (int)(textFont->getHeight() * textScale * 1.5f));
                    g->pushTransform();
                    {
                        const int leftTrans = engine->getScreenWidth() - largestLineWidth;
                        g->translate(leftTrans, 0);
                        if(!this->textLines[i].textLeftAligned.isEmpty())
                            g->drawString(textFont, this->textLines[i].textLeftAligned,
                                          TextShadow{.col_text = textColor, .offs_px = std::round(1.f * env->getDPIScale())});

                        const int rightTrans =
                            (engine->getScreenWidth() - (this->textLines[i].widthRight * textScale)) - leftTrans;
                        g->translate(rightTrans, 0);
                        g->drawString(textFont, this->textLines[i].textRightAligned,
                                      TextShadow{.col_text = textColor, .offs_px = std::round(1.f * env->getDPIScale())});
                    }
                    g->popTransform();
                }
            }
            g->popTransform();
        }

        this->textLines.clear();
        this->engineTextLines.clear();
        this->appTextLines.clear();
    }

    // draw profiler node tree extended details
    if(cv::debug_vprof.getBool()) {
        VPROF_BUDGET("DebugText", VPROF_BUDGETGROUP_DRAW);

        g->setColor(0xffcccccc);
        g->pushTransform();
        {
            g->translate(0, this->font->getHeight());

            g->drawString(this->font, fmt::format("{:d} nodes"_cf, this->profile->getNumNodes()));
            g->translate(0, this->font->getHeight() * 1.5f);

            g->drawString(this->font, fmt::format("{:d} groups"_cf, this->profile->getNumGroups()));
            g->translate(0, this->font->getHeight() * 1.5f);

            g->drawString(this->font, "----------------------------------------------------");
            g->translate(0, this->font->getHeight() * 1.5f);

            for(int i = this->nodes.size() - 1; i >= 0; i--) {
                g->pushTransform();
                {
                    g->translate(this->font->getHeight() * 3 * (this->nodes[i].depth - 1), 0);
                    g->drawString(this->font,
                                  fmt::format("[{:s}] - {:s} = {:f} ms"_cf, this->nodes[i].node->getName(),
                                              this->profile->getGroupName(this->nodes[i].node->getGroupID()),
                                              this->nodes[i].node->getTimeLastFrame() * 1000.0));
                }
                g->popTransform();

                g->translate(0, this->font->getHeight() * 1.5f);
            }

            g->drawString(this->font, "----------------------------------------------------");
            g->translate(0, this->font->getHeight() * 1.5f);

            for(int i = 0; i < this->profile->getNumGroups(); i++) {
                const char *groupName = this->profile->getGroupName(i);
                const double sum = this->profile->sumTimes(i);

                g->drawString(this->font, fmt::format("{:s} = {:f} ms"_cf, groupName, sum * 1000.0));
                g->translate(0, this->font->getHeight() * 1.5f);
            }
        }
        g->popTransform();
    }

    // draw extended spike details tree (profiler node snapshot)
    if(cv::vprof_spike.getBool() && !cv::debug_vprof.getBool()) {
        if(this->spike.node.node != nullptr) {
            if(cv::vprof_spike.getInt() == 2) {
                VPROF_BUDGET("DebugText", VPROF_BUDGETGROUP_DRAW);

                g->setColor(0xffcccccc);
                g->pushTransform();
                {
                    g->translate(0, this->font->getHeight());

                    for(int i = this->spikeNodes.size() - 1; i >= 0; i--) {
                        g->pushTransform();
                        {
                            g->translate(this->font->getHeight() * 3 * (this->spikeNodes[i].node.depth - 1), 0);
                            g->drawString(
                                this->font,
                                fmt::format("[{:s}] - {:s} = {:f} ms"_cf, this->spikeNodes[i].node.node->getName(),
                                            this->profile->getGroupName(this->spikeNodes[i].node.node->getGroupID()),
                                            this->spikeNodes[i].timeLastFrame * 1000.0));
                        }
                        g->popTransform();

                        g->translate(0, this->font->getHeight() * 1.5f);
                    }
                }
                g->popTransform();
            }
        }
    }

    // draw graph
    if(cv::vprof_graph.getBool()) {
        VPROF_BUDGET("LineGraph", VPROF_BUDGETGROUP_DRAW);

        const int width = getGraphWidth();
        const int height = getGraphHeight();
        const int margin = cv::vprof_graph_margin.getFloat() * env->getDPIScale();

        const int xPos = engine->getScreenWidth() - width - margin;
        const int yPos = engine->getScreenHeight() - height - margin +
                         (mouse->isMiddleDown() ? mouse->getPos().y - engine->getScreenHeight() : 0);

        // draw background
        g->setColor(0xaa000000);
        g->fillRect(xPos - 1, yPos - 1, width + 1, height + 1);
        g->setColor(0xff777777);
        g->drawRect(xPos - 1, yPos - 1, width + 1, height + 1);

        // draw lines
        g->setColor(0xff00aa00);
        g->pushTransform();
        {
            const int stride = 2 * this->iPrevVaoGroups;

            // behind
            this->lineVao->setDrawRange(this->iCurLinePos * stride, this->iPrevVaoWidth * stride);
            g->translate(xPos + 1 - this->iCurLinePos, yPos + height);
            g->drawVAO(this->lineVao);

            // forward
            this->lineVao->setDrawRange(0, this->iCurLinePos * stride);
            g->translate(this->iPrevVaoWidth, 0);
            g->drawVAO(this->lineVao);
        }
        g->popTransform();

        // draw labels
        if(keyboard->isControlDown()) {
            const int margin = 3 * env->getDPIScale();

            // y-axis range
            g->pushTransform();
            {
                g->translate((int)(xPos + margin), (int)(yPos + this->font->getHeight() + margin));
                g->drawString(this->font, fmt::format("{:g} ms"_cf, cv::vprof_graph_range_max.getFloat()),
                              TextShadow{});

                g->translate(0, (int)(height - this->font->getHeight() - 2 * margin));
                g->drawString(this->font, "0 ms", TextShadow{});
            }
            g->popTransform();

            // colored group names
            g->pushTransform();
            {
                const int padding = 6 * env->getDPIScale();

                g->translate((int)(xPos - 3 * margin), (int)(yPos + height - padding));

                for(size_t i = 1; i < this->groups.size(); i++) {
                    const int stringWidth = (int)(this->font->getStringWidth(this->groups[i].name));
                    g->translate(-stringWidth, 0);
                    g->drawString(
                        this->font, this->groups[i].name,
                        TextShadow{.col_text = this->groups[i].color, .offs_px = std::round(1.f * env->getDPIScale())});
                    g->translate(stringWidth, (int)(-this->font->getHeight() - padding));
                }
            }
            g->popTransform();
        }

        // draw top spike text above graph
        if(cv::vprof_spike.getBool() && !cv::debug_vprof.getBool()) {
            if(this->spike.node.node != nullptr) {
                if(cv::vprof_spike.getInt() == 1) {
                    const int margin = 6 * env->getDPIScale();

                    g->setColor(0xffcccccc);
                    g->pushTransform();
                    {
                        g->translate((int)(xPos + margin), (int)(yPos - 2 * margin));
                        /// g->drawString(fmt::format("[{:s}] = {:g} ms", this->spike.node.node->getName(),
                        /// m_spike.timeLastFrame * 1000.0), TextShadow{.textColor = this->groups[m_spike.node.node->getGroupID()].color});
                        g->drawString(this->font,
                                      fmt::format("Spike = {:g} ms"_cf, this->spike.timeLastFrame * 1000.0));
                    }
                    g->popTransform();
                }
            }
        }
    }
}

void VisualProfiler::update(CBaseUIEventCtx &c) {
    VPROF_BUDGET("VisualProfiler::update", VPROF_BUDGETGROUP_UPDATE);
    CBaseUIElement::update(c);
    if(!cv::vprof.getBool() || !this->bVisible) return;

    if(cv::vprof_display_mode.getVal<INFO_BLADE_DISPLAY_MODE>() == INFO_BLADE_DISPLAY_MODE::CPU_RAM_INFO) {
        this->infoGatherer->update();
    }

    const bool isFrozen = (keyboard->isShiftDown() && (!this->bRequiresAltShiftKeysToFreeze || keyboard->isAltDown()));

    if(cv::debug_vprof.getBool() || cv::vprof_spike.getBool()) {
        if(!isFrozen) {
            SPIKE spike{
                .node = {.node = nullptr, .depth = -1},
                .timeLastFrame = 0.0,
                .id = this->spikeIDCounter++,
            };

            // run regular debug node collector
            this->nodes.clear();
            collectProfilerNodesRecursive(this->profile->getRoot(), 0, this->nodes, spike);

            // run spike collector and updater
            if(cv::vprof_spike.getBool()) {
                const int graphWidth = getGraphWidth();

                this->spikes.push_back(spike);

                if(this->spikes.size() > graphWidth) this->spikes.erase(this->spikes.begin());

                SPIKE &newSpike = this->spikes[0];

                for(auto &spike : this->spikes) {
                    if(spike.timeLastFrame > newSpike.timeLastFrame) newSpike = spike;
                }

                if(newSpike.id != this->spike.id) {
                    const bool isNewSpikeLarger = (newSpike.timeLastFrame > this->spike.timeLastFrame);

                    this->spike = newSpike;

                    // NOTE: since we only store 1 spike snapshot, once that is erased (this->spikes.size() >
                    // graphWidth) and we have to "fall back" to a "lower" spike, we don't have any data on that lower
                    // spike anymore so, we simply only create a new snapshot if we have a new larger spike (since that
                    // is guaranteed to be the currently active one, i.e. going through node data in m_profile will
                    // return its data) (storing graphWidth amounts of snapshots seems unnecessarily wasteful, and I
                    // like this solution)
                    if(isNewSpikeLarger) {
                        this->spikeNodes.clear();
                        collectProfilerNodesSpikeRecursive(this->spike.node.node, 1, this->spikeNodes);
                    }
                }
            }
        }
    }

    // lazy rebuild group/color list
    if(this->groups.size() < this->profile->getNumGroups()) {
        // reset
        this->iDrawGroupID = -1;
        this->iDrawSwapBuffersGroupID = -1;

        const int curNumGroups = this->groups.size();
        const int actualNumGroups = this->profile->getNumGroups();

        for(int i = curNumGroups; i < actualNumGroups; i++) {
            GROUP group;

            group.name = this->profile->getGroupName(i);
            group.id = i;

            // hardcoded colors for some groups
            if(strcmp(group.name, VPROF_BUDGETGROUP_ROOT) ==
               0)  // NOTE: VPROF_BUDGETGROUP_ROOT is used for drawing the profiling overhead time if
                   // cv::vprof_graph_draw_overhead is enabled
                group.color = 0xffffffff;
            else if(strcmp(group.name, VPROF_BUDGETGROUP_SLEEP) == 0)
                group.color = 0xff5555bb;
            else if(strcmp(group.name, VPROF_BUDGETGROUP_BETWEENFRAMES) == 0)
                group.color = 0xff7777bb;
            else if(strcmp(group.name, VPROF_BUDGETGROUP_EVENTS) == 0)
                group.color = 0xffffff00;
            else if(strcmp(group.name, VPROF_BUDGETGROUP_UPDATE) == 0)
                group.color = 0xff00bb00;
            else if(strcmp(group.name, VPROF_BUDGETGROUP_DRAW) == 0) {
                group.color = 0xffbf6500;
                this->iDrawGroupID = group.id;
            } else if(strcmp(group.name, VPROF_BUDGETGROUP_DRAW_SWAPBUFFERS) == 0) {
                group.color = 0xffff0000;
                this->iDrawSwapBuffersGroupID = group.id;
            } else
                group.color = 0xff00ffff;  // default to turquoise

            this->groups.push_back(group);
        }
    }

    // and handle line updates
    {
        const int numGroups = this->groups.size();
        const int graphWidth = getGraphWidth();
        const int graphHeight = getGraphHeight();
        const float maxRange = cv::vprof_graph_range_max.getFloat();
        const float alpha = cv::vprof_graph_alpha.getFloat();

        // lazy rebuild line vao if parameters change
        if(this->bScheduledForceRebuildLineVao || this->iPrevVaoWidth != graphWidth ||
           this->iPrevVaoHeight != graphHeight || this->iPrevVaoGroups != numGroups ||
           this->fPrevVaoMaxRange != maxRange || this->fPrevVaoAlpha != alpha) {
            this->bScheduledForceRebuildLineVao = false;

            this->iPrevVaoWidth = graphWidth;
            this->iPrevVaoHeight = graphHeight;
            this->iPrevVaoGroups = numGroups;
            this->fPrevVaoMaxRange = maxRange;
            this->fPrevVaoAlpha = alpha;

            this->lineVao->release();

            // preallocate 2 vertices per line
            for(int x = 0; x < graphWidth; x++) {
                for(int g = 0; g < numGroups; g++) {
                    Color color = this->groups[g].color;
                    color.setA(this->fPrevVaoAlpha);

                    // this->lineVao->addVertex(x, -(((float)graphHeight)/(float)numGroups)*g, 0);
                    this->lineVao->addVertex(x, 0, 0);
                    this->lineVao->addColor(color);

                    // this->lineVao->addVertex(x, -(((float)graphHeight)/(float)numGroups)*(g + 1), 0);
                    this->lineVao->addVertex(x, 0, 0);
                    this->lineVao->addColor(color);
                }
            }

            // and bake
            resourceManager->loadResource(this->lineVao);
        }

        // regular line update
        if(!isFrozen) {
            if(this->lineVao->isReady()) {
                // one new multi-line per frame
                this->iCurLinePos = this->iCurLinePos % graphWidth;

                // if enabled, calculate and draw overhead
                // the overhead is the time spent between not having any profiler node active/alive, and should always
                // be <= 0 it is usually slightly negative (in the order of 10 microseconds, includes rounding errors
                // and timer inaccuracies) if the overhead ever gets positive then either there are no nodes covering
                // all paths below VPROF_MAIN(), or there is a serious problem with measuring time via
                // Timing::getTimeReal()
                double profilingOverheadTime = 0.0;
                if(cv::vprof_graph_draw_overhead.getBool()) {
                    const int rootGroupID = 0;
                    double sumGroupTimes = 0.0;
                    {
                        for(size_t i = 1; i < this->groups.size(); i++)  // NOTE: start at 1, ignore rootGroupID
                        {
                            sumGroupTimes += this->profile->sumTimes(this->groups[i].id);
                        }
                    }
                    profilingOverheadTime = std::max(0.0, this->profile->sumTimes(rootGroupID) - sumGroupTimes);
                }

                // go through every group and build the new multi-line
                int heightCounter = 0;
                for(int i = 0; i < numGroups && (size_t)i < this->groups.size(); i++) {
                    const double rawDuration =
                        (i == 0 ? profilingOverheadTime : this->profile->sumTimes(this->groups[i].id));
                    const double duration =
                        (i == this->iDrawGroupID
                             ? rawDuration - this->profile->sumTimes(this->iDrawSwapBuffersGroupID)
                             : rawDuration);  // special case: hardcoded fix for nested groups (only draw + swap atm)
                    const int lineHeight = (int)(((duration * 1000.0) / (double)maxRange) * (double)graphHeight);

                    this->lineVao->setVertex(this->iCurLinePos * numGroups * 2 + i * 2, this->iCurLinePos,
                                             heightCounter);
                    this->lineVao->setVertex(this->iCurLinePos * numGroups * 2 + i * 2 + 1, this->iCurLinePos,
                                             heightCounter - lineHeight);

                    heightCounter -= lineHeight;
                }

                // re-bake
                resourceManager->loadResource(this->lineVao);

                this->iCurLinePos++;
            }
        }
    }
}

void VisualProfiler::incrementInfoBladeDisplayMode() {
    cv::vprof_display_mode.setValue((cv::vprof_display_mode.getInt() + 1) % (int)INFO_BLADE_DISPLAY_MODE::COUNT);
}

void VisualProfiler::decrementInfoBladeDisplayMode() {
    if(cv::vprof_display_mode.getInt() - 1 < (int)INFO_BLADE_DISPLAY_MODE::DEFAULT)
        cv::vprof_display_mode.setValue((int)INFO_BLADE_DISPLAY_MODE::COUNT - 1);
    else
        cv::vprof_display_mode.setValue(cv::vprof_display_mode.getInt() - 1);
}

void VisualProfiler::addInfoBladeEngineTextLine(const UString &text) {
    if(!cv::vprof.getBool() || !this->bVisible ||
       cv::vprof_display_mode.getVal<INFO_BLADE_DISPLAY_MODE>() != INFO_BLADE_DISPLAY_MODE::ENGINE_INFO)
        return;

    this->engineTextLines.push_back(text);
}

void VisualProfiler::addInfoBladeAppTextLine(const UString &text) {
    if(!cv::vprof.getBool() || !this->bVisible ||
       cv::vprof_display_mode.getVal<INFO_BLADE_DISPLAY_MODE>() != INFO_BLADE_DISPLAY_MODE::APP_INFO)
        return;

    this->appTextLines.push_back(text);
}

void VisualProfiler::setProfile(ProfilerProfile *profile) {
    this->profile = profile;

    // force everything to get re-built for the new profile with the next frame
    {
        this->groups.clear();

        this->bScheduledForceRebuildLineVao = true;
    }
}

bool VisualProfiler::isEnabled() { return cv::vprof.getBool(); }

void VisualProfiler::collectProfilerNodesRecursive(const ProfilerNode *node, int depth, std::vector<NODE> &nodes,
                                                   SPIKE &spike) {
    if(node == nullptr) return;

    // recursive call
    ProfilerNode *child = node->getChild();
    while(child != nullptr) {
        collectProfilerNodesRecursive(child, depth + 1, nodes, spike);
        child = child->getSibling();
    }

    // add node (ignore root 0)
    if(depth > 0) {
        NODE entry{.node = node, .depth = depth};

        nodes.push_back(entry);

        const double timeLastFrame = node->getTimeLastFrame();
        if(spike.node.node == nullptr || timeLastFrame > spike.timeLastFrame) {
            spike.node = entry;
            spike.timeLastFrame = timeLastFrame;
        }
    }
}

void VisualProfiler::collectProfilerNodesSpikeRecursive(const ProfilerNode *node, int depth,
                                                        std::vector<SPIKE> &spikeNodes) {
    if(node == nullptr) return;

    // recursive call
    ProfilerNode *child = node->getChild();
    while(child != nullptr) {
        collectProfilerNodesSpikeRecursive(child, depth + 1, spikeNodes);
        child = child->getSibling();
    }

    // add spike node (ignore root 0)
    if(depth > 0) {
        SPIKE spike{
            .node = {.node = node, .depth = depth},
            .timeLastFrame = node->getTimeLastFrame(),
        };

        spikeNodes.push_back(spike);
    }
}

int VisualProfiler::getGraphWidth() { return (cv::vprof_graph_width.getFloat() * env->getDPIScale()); }

int VisualProfiler::getGraphHeight() { return (cv::vprof_graph_height.getFloat() * env->getDPIScale()); }

void VisualProfiler::addTextLine(const UString &text, McFont *font, std::vector<TEXT_LINE> &textLines) {
    textLines.push_back({.textLeftAligned = {},
                         .textRightAligned = text,
                         .widthLeft = 0,
                         .widthRight = (int)font->getStringWidth(text)});
}

void VisualProfiler::addTextLine(const UString &textLeft, const UString &textRight, McFont *font,
                                 std::vector<TEXT_LINE> &textLines) {
    textLines.push_back({.textLeftAligned = textLeft,
                         .textRightAligned = textRight,
                         .widthLeft = (int)(font->getStringWidth(textLeft) * 1.1f),  // spacing
                         .widthRight = (int)font->getStringWidth(textRight)});
}
