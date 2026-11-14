// Copyright (c) 2026, WH, All rights reserved.
#include "SetupPanel.h"

#include "CBaseUIColorButton.h"
#include "CBaseUINumberField.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "CBaseUIStyledTextbox.h"
#include "ContainerRanges.h"
#include "EditorState.h"
#include "Environment.h"
#include "Form.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "MapFiles.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "Parsing.h"
#include "SString.h"
#include "Skin.h"
#include "UI.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <string_view>
#include <utility>
#include <vector>

using Kind = neomod::BeatmapFile::SectionKind;
using Step = Document::Action;
using Change = Document::Change;

namespace {
std::string_view valueOf(const Document &document, Kind kind, std::string_view key) {
    return document.getFile().getValue(kind, key).value_or(std::string_view{});
}

f64 numberOf(const Document &document, Kind kind, std::string_view key, f64 otherwise) {
    return document.getFile().getValue<f64>(kind, key).value_or(otherwise);
}

// what the romanised fields take
bool isAscii(char32_t character) { return character >= 32 && character <= 126; }
bool isAscii(std::string_view text) {
    return Mc::ranges::all_of(text, [](char c) { return isAscii((char32_t)(unsigned char)c); });
}

constexpr uSz MAX_COLOURS{8};

// Combo1 to Combo8 in the file, as read by the game
std::array<std::optional<Color>, MAX_COLOURS> colourKeys(const Document &document) {
    std::array<std::optional<Color>, MAX_COLOURS> keys;
    neomod::BeatmapFile::Colour colour;
    for(const auto line : document.getFile().getEntries(Kind::COLOURS)) {
        u8 number{0};
        if(neomod::BeatmapFile::parse(line.text, colour) && Parsing::parse(colour.name, "Combo", &number) &&
           number >= 1 && number <= MAX_COLOURS) {
            keys[number - 1] = rgb(colour.r, colour.g, colour.b);
        }
    }
    return keys;
}

// the colours a map or skin has as they come in its file (the first combo has the second one), in the order combos get
// them
std::vector<Color> inPlayOrder(std::span<const Color> inFile) {
    std::vector<Color> played(inFile.size());
    for(uSz i = 0; i < played.size(); i++) played[i] = inFile[(i + 1) % inFile.size()];
    return played;
}

// the map combo colours in the order combos get them (none: the skin colours are used)
std::vector<Color> comboColours(const Document &document) { return inPlayOrder(document.getPrimitives().combocolors); }

// makes them the map combo colours: the keys whose colours they aren't yet are written, the ones past them go
void setComboColours(Change &c, const Document &document, std::span<const Color> played) {
    std::vector<Color> inFile(played.size());
    for(uSz i = 0; i < played.size(); i++) inFile[(i + 1) % played.size()] = played[i];

    const auto keys = colourKeys(document);
    for(uSz k = 0; k < MAX_COLOURS; k++) {
        const std::string key = fmt::format("Combo{}", k + 1);
        if(k >= inFile.size()) {
            if(keys[k]) c.setValue(Kind::COLOURS, key, std::nullopt);
        } else if(keys[k] != inFile[k]) {
            c.setValue(Kind::COLOURS, key, fmt::format("{},{},{}", inFile[k].r, inFile[k].g, inFile[k].b));
        }
    }
}
}  // namespace

SetupPanel::SetupPanel(EditorState &state, CBaseUIPopupLayer &popups)
    : CBaseUIBox(Axis::ROW, "editor_setup"), state(state) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(2.f * style.metrics.padding);

    // a name as it's written and its romanised form: while the name is ASCII the romanised one is the same and follows
    // it, otherwise it's set by itself
    auto *metadata = new Form(state, popups, "editor_setup_metadata", _("Metadata"));
    const auto name = [&](const char *field, const char *romanisedField, std::string label, std::string romanisedLabel,
                          std::string_view key, std::string_view unicodeKey) {
        const auto written = [key, unicodeKey](const Document &document) {
            const std::string_view unicode = valueOf(document, Kind::METADATA, unicodeKey);
            return unicode.empty() ? valueOf(document, Kind::METADATA, key) : unicode;
        };
        metadata->addText(
            field, std::move(label), Step::METADATA,
            [written](const Document &document) { return std::string{written(document)}; },
            [this, key, unicodeKey](Change &c, std::string_view text) {
                // a file that never needed the key for the name as written doesn't get one for an ASCII name
                const bool ascii = isAscii(text);
                if(!ascii || this->state.document->getFile().getValue(Kind::METADATA, unicodeKey)) {
                    c.setValue(Kind::METADATA, unicodeKey, text);
                }
                if(ascii) c.setValue(Kind::METADATA, key, text);
            });
        metadata
            ->addText(
                romanisedField, std::move(romanisedLabel), Step::METADATA,
                [key](const Document &document) { return std::string{valueOf(document, Kind::METADATA, key)}; },
                [key](Change &c, std::string_view text) { c.setValue(Kind::METADATA, key, text); },
                [written](const Document &document) { return !isAscii(written(document)); })
            ->setFilter([](char32_t character) { return isAscii(character); });
    };
    name("editor_setup_artist", "editor_setup_artist_romanised", _("Artist"), _("Romanised Artist"), "Artist",
         "ArtistUnicode");
    name("editor_setup_title", "editor_setup_title_romanised", _("Title"), _("Romanised Title"), "Title",
         "TitleUnicode");
    const std::array<std::tuple<const char *, std::string, std::string_view>, 4> texts{
        {{"editor_setup_creator", _("Creator"), "Creator"},
         {"editor_setup_version", _("Difficulty Name"), "Version"},
         {"editor_setup_source", _("Source"), "Source"},
         {"editor_setup_tags", _("Tags"), "Tags"}}};
    for(const auto &[field, label, key] : texts) {
        CBaseUIStyledTextbox *text = metadata->addText(
            field, label, Step::METADATA,
            [key](const Document &document) { return std::string{valueOf(document, Kind::METADATA, key)}; },
            [key](Change &c, std::string_view text) { c.setValue(Kind::METADATA, key, text); });
        if(key == "Version") this->version = text;
    }

    // a number in the file: what it has, or what the game takes without it
    const auto number = [](Form *form, const char *field, std::string label, Step action, Kind kind,
                           std::string_view key, f64 otherwise) {
        return form->addNumber(
            field, std::move(label), action,
            [kind, key, otherwise](const Document &document) { return numberOf(document, kind, key, otherwise); },
            [kind, key](Change &c, f64 value) { c.setValue(kind, key, fmt::format("{}", value)); });
    };

    auto *difficulty = new Form(state, popups, "editor_setup_difficulty", _("Difficulty"));
    const std::array<std::tuple<const char *, std::string, std::string_view>, 3> rates{
        {{"editor_setup_hp", _("HP Drain Rate"), "HPDrainRate"},
         {"editor_setup_cs", _("Circle Size"), "CircleSize"},
         {"editor_setup_od", _("Overall Difficulty"), "OverallDifficulty"}}};
    for(const auto &[field, label, key] : rates) {
        number(difficulty, field, label, Step::DIFFICULTY, Kind::DIFFICULTY, key, 5.0)
            ->setRange(0.0, 10.0)
            ->setStep(0.1, 0.1)
            ->setDecimals(1);
    }
    // a file without an approach rate uses its overall difficulty
    difficulty
        ->addNumber(
            "editor_setup_ar", _("Approach Rate"), Step::DIFFICULTY,
            [](const Document &document) {
                return numberOf(document, Kind::DIFFICULTY, "ApproachRate",
                                numberOf(document, Kind::DIFFICULTY, "OverallDifficulty", 5.0));
            },
            [](Change &c, f64 value) { c.setValue(Kind::DIFFICULTY, "ApproachRate", fmt::format("{}", value)); })
        ->setRange(0.0, 10.0)
        ->setStep(0.1, 0.1)
        ->setDecimals(1);
    // without a value, what the game plays
    number(difficulty, "editor_setup_slider_velocity", _("Slider Velocity"), Step::DIFFICULTY, Kind::DIFFICULTY,
           "SliderMultiplier", 1.0)
        ->setRange(0.4, 3.6)
        ->setStep(0.1, 0.01)
        ->setDecimals(2);
    number(difficulty, "editor_setup_tick_rate", _("Slider Tick Rate"), Step::DIFFICULTY, Kind::DIFFICULTY,
           "SliderTickRate", 1.0)
        ->setRange(0.5, 8.0)
        ->setStep(0.5, 0.5)
        ->setDecimals(2);
    number(difficulty, "editor_setup_stack_leniency", _("Stack Leniency"), Step::DIFFICULTY, Kind::GENERAL,
           "StackLeniency", 0.7)
        ->setRange(0.0, 1.0)
        ->setStep(0.1, 0.1)
        ->setDecimals(1);

    auto *general = new Form(state, popups, "editor_setup_general", _("General"));
    // the image behind the map: a file to take (copied into the map folder), and its name under it
    auto *chooseBackground = new CBaseUIStyledButton(_("Choose..."), "editor_setup_background_choose");
    chooseBackground->setClickCallback([this] {
        // the filter name, then its extensions
        const std::string filter = std::string{_("Images")} + '\0' + "jpg;jpeg;png" + '\0';
        this->fileDialog = env->openFileWindow(
            [this](const std::vector<std::string> &paths) {
                if(!paths.empty()) this->useBackground(paths.front());
            },
            filter.c_str(), _("Background"));
    });
    general->addRow(_("Background"), chooseBackground, BoxItem::flex());
    this->background = new CBaseUIStyledLabel({}, "editor_setup_background");
    general->add(this->background, BoxItem::fit());
    const auto milliseconds = [&](const char *field, std::string label, std::string_view key, f64 otherwise) {
        return general
            ->addNumber(
                field, std::move(label), Step::MAP_SETTINGS,
                [key, otherwise](const Document &document) {
                    return numberOf(document, Kind::GENERAL, key, otherwise);
                },
                [key](Change &c, f64 value) { c.setValue(Kind::GENERAL, key, fmt::format("{}", (i64)value)); })
            ->setStep(100.0, 10.0)
            ->setSuffix(_(" ms"));
    };
    milliseconds("editor_setup_lead_in", _("Audio Lead-In"), "AudioLeadIn", 0.0)->setRange(0.0, 10000.0);
    // -1: none, the song select picks a place
    milliseconds("editor_setup_preview_time", _("Preview Point"), "PreviewTime", -1.0)
        ->setRange(-1.0, 36000000.0)
        ->setNamedValue(-1.0, _("None"));
    general->addButton("editor_setup_preview_now", _("Use Current Time"), Step::MAP_SETTINGS, [this](Change &c) {
        c.setValue(Kind::GENERAL, "PreviewTime", fmt::format("{}", this->state.clock.getTime()));
    });
    // as named by the file; without one it's the first
    static constexpr std::array<std::string_view, 3> SAMPLE_SETS{"Normal", "Soft", "Drum"};
    general->addChoice(
        "editor_setup_sample_set", _("Sample Set"), {_("Normal"), _("Soft"), _("Drum")}, Step::MAP_SETTINGS,
        [](const Document &document) {
            const std::string set = SString::to_lower(valueOf(document, Kind::GENERAL, "SampleSet"));
            if(set.empty()) return 0;
            const auto at =
                Mc::ranges::find_if(SAMPLE_SETS, [&](std::string_view name) { return SString::to_lower(name) == set; });
            return at != SAMPLE_SETS.end() ? (int)(at - SAMPLE_SETS.begin()) : -1;
        },
        [](Change &c, int index) { c.setValue(Kind::GENERAL, "SampleSet", SAMPLE_SETS[(uSz)index]); });
    // the numbers in the file: 1 is the usual speed, which a file without the key has
    general->addChoice(
        "editor_setup_countdown", _("Countdown"), {_("None"), _("Normal"), _("Half Speed"), _("Double Speed")},
        Step::MAP_SETTINGS,
        [](const Document &document) {
            const int countdown = (int)numberOf(document, Kind::GENERAL, "Countdown", 1.0);
            return countdown >= 0 && countdown <= 3 ? countdown : -1;
        },
        [](Change &c, int index) { c.setValue(Kind::GENERAL, "Countdown", fmt::format("{}", index)); });
    general
        ->addNumber(
            "editor_setup_countdown_offset", _("Countdown Offset"), Step::MAP_SETTINGS,
            [](const Document &document) { return numberOf(document, Kind::GENERAL, "CountdownOffset", 0.0); },
            [](Change &c, f64 value) { c.setValue(Kind::GENERAL, "CountdownOffset", fmt::format("{}", (i64)value)); },
            [](const Document &document) { return numberOf(document, Kind::GENERAL, "Countdown", 1.0) != 0.0; })
        ->setRange(0.0, 3.0)
        ->setStep(1.0, 1.0)
        ->setSuffix(_(" beats"));
    // on and off; `always`: the key is there either way, otherwise only while it's on
    const auto toggle = [&](const char *field, std::string label, std::string_view key, bool always) {
        general->addToggle(
            field, std::move(label), Step::MAP_SETTINGS,
            [key](const Document &document) { return numberOf(document, Kind::GENERAL, key, 0.0) != 0.0; },
            [key, always](Change &c, bool on) {
                c.setValue(Kind::GENERAL, key,
                           on       ? std::optional<std::string_view>{"1"}
                           : always ? "0"
                                    : std::optional<std::string_view>{});
            });
    };
    toggle("editor_setup_widescreen", _("Widescreen Storyboard"), "WidescreenStoryboard", true);
    toggle("editor_setup_letterbox", _("Letterbox in Breaks"), "LetterboxInBreaks", true);
    toggle("editor_setup_epilepsy", _("Epilepsy Warning"), "EpilepsyWarning", false);
    toggle("editor_setup_samples_rate", _("Samples Match Playback Rate"), "SamplesMatchPlaybackRate", false);

    // the combo colours, in the order combos get them: a row each, shown while the map has that many
    auto *colours = new Form(state, popups, "editor_setup_colours", _("Combo Colours"));
    this->noColours = new CBaseUIStyledLabel(_("The skin colours are used."));
    colours->add(this->noColours, BoxItem::fit());
    for(uSz i = 0; i < this->colourRows.size(); i++) {
        CBaseUIColorButton *colour = colours->makeColour(
            fmt::format("editor_setup_colour_{}", i + 1), Step::COLOURS,
            [i](const Document &document) {
                const std::vector<Color> played = comboColours(document);
                return i < played.size() ? played[i] : Color{0xff000000};
            },
            [this, i](Change &c, Color colour) {
                const Document &document = *this->state.document;
                std::vector<Color> played = comboColours(document);
                if(i >= played.size()) return;
                played[i] = colour;
                setComboColours(c, document, played);
            });
        auto *row = new CBaseUIBox(Axis::ROW, fmt::format("editor_setup_colour_row_{}", i + 1));
        row->setGap(style.metrics.gap);
        row->add(new CBaseUIStyledLabel(fmt::format("{}", i + 1)), BoxItem::fixed(14.f))
            ->add(colour, BoxItem::flex())
            ->add(colours->makeButton(fmt::format("editor_setup_colour_{}_remove", i + 1), "x", Step::COLOURS,
                                      [this, i](Change &c) {
                                          const Document &document = *this->state.document;
                                          std::vector<Color> played = comboColours(document);
                                          if(i >= played.size()) return;
                                          played.erase(played.begin() + (sSz)i);
                                          setComboColours(c, document, played);
                                      }),
                  BoxItem::fit());
        this->colourRows[i] = row;
        colours->add(row, BoxItem::fit());
    }
    // a map without colours starts with the skin colours, so that nothing looks different until they're changed
    this->addColour = colours->makeButton(
        "editor_setup_colour_add", {}, Step::COLOURS,
        [this](Change &c) {
            const Document &document = *this->state.document;
            std::vector<Color> played = comboColours(document);
            if(!played.empty()) {
                played.push_back(rgb(240, 240, 240));
            } else if(const Skin *skin = osu->getSkin(); skin && !skin->c_combo_colors.empty()) {
                const std::span<const Color> skins{skin->c_combo_colors};
                played = inPlayOrder(skins.first(std::min(skins.size(), MAX_COLOURS)));
            } else {
                played.push_back(rgb(240, 240, 240));
            }
            setComboColours(c, document, played);
        },
        [](const Document &document) { return comboColours(document).size() < MAX_COLOURS; });
    colours->add(this->addColour, BoxItem::fit());

    this->forms = {metadata, difficulty, general, colours};
    this->add(metadata, BoxItem::flex(1.5f))
        ->add(difficulty, BoxItem::flex())
        ->add(general, BoxItem::flex(1.2f))
        ->add(colours, BoxItem::flex(1.1f));
}

SetupPanel::~SetupPanel() = default;

void SetupPanel::tick() {
    // a box ticks what it doesn't show too
    if(!this->isVisible()) return;

    if(const Document *document = this->state.document.get()) {
        if(const std::string_view name =
               document->getBackground().empty() ? std::string_view{_("None")} : document->getBackground();
           this->background->getText() != name) {
            this->background->setText(std::string{name});
        }
        const uSz count = comboColours(*document).size();
        for(uSz i = 0; i < this->colourRows.size(); i++) this->colourRows[i]->setVisible(i < count);
        this->noColours->setVisible(count == 0);
        if(const char *label = count == 0 ? _("Use Custom Colours") : _("Add Colour");
           this->addColour->getText() != label) {
            this->addColour->setText(label);
        }
    }
    CBaseUIBox::tick();
}

void SetupPanel::commit() {
    for(Form *form : this->forms) form->commit();
}

void SetupPanel::useBackground(const std::string &path) {
    if(this->state.copyFirst([this, path] { this->useBackground(path); })) return;
    Document &document = *this->state.document;
    const std::string name = Environment::getFileNameFromFilePath(path);
    // written quoted, on a line of its own
    if(Mc::ranges::any_of(name, [](char c) { return (unsigned char)c < 32 || c == '"'; })) {
        ui->getNotificationOverlay()->addNotification(
            tformat("{} can't be used: its name can't be written into a map.", name));
        return;
    }
    const auto taken = MapFiles::takeIn(MapFiles::folderOf(document.getLocation()), path);
    if(!taken) {
        ui->getNotificationOverlay()->addToast(tformat("{} couldn't be copied into the map folder.", name),
                                               ERROR_TOAST);
        return;
    }
    this->state.edit(Step::BACKGROUND, [&](Change &c) { c.setBackground(*taken); });
}

void SetupPanel::editVersion() {
    // it shows the map name before it's selected
    for(Form *form : this->forms) form->refresh();
    this->version->visit();
}

void SetupPanel::onKeyDown(KeyboardEvent &e) {
    CBaseUIBox::onKeyDown(e);
    if(e.isConsumed() || !this->isVisible() || e != KEY_TAB) return;
    if(Form::visitNext(this->forms, e.getModifiers() & KEYMOD_SHIFT)) e.consume();
}
