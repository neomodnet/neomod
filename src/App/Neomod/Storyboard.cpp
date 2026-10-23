// Copyright (c) 2025, kiwec, All rights reserved.
#include "Storyboard.h"

#include "AnimationHandler.h"
#include "AsyncPool.h"
#include "ContainerRanges.h"
#include "BeatmapFile/BeatmapFile.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Graphics.h"
#include "Hashing.h"
#include "Image.h"
#include "Logging.h"
#include "OsuConVars.h"
#include "Parsing.h"
#include "ResourceManager.h"
#include "SString.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "crypto.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

using neomod::BeatmapFile;
using Ease = AnimationHandler::Ease;
using Layer = Storyboard::Layer;

namespace {

constexpr f64 INF = std::numeric_limits<f64>::infinity();
constexpr u32 NO_FILE = std::numeric_limits<u32>::max();

// a property going from one value to another over [start, end]
template <typename T>
struct Command {
    f64 start;
    f64 end;
    T from;
    T to;
    Ease easing;
};

// the commands of a sprite, a loop or a trigger, per property, sorted by start (then end) time
struct Group {
    std::vector<Command<f32>> x, y, scale, rotation, alpha;
    std::vector<Command<vec2>> vectorScale;
    std::vector<Command<vec3>> colour;
    std::vector<Command<bool>> flipH, flipV, additive;
    f64 start{INF};
    f64 end{-INF};

    [[nodiscard]] bool empty() const { return this->start > this->end; }

    template <typename T>
    void add(std::vector<Command<T>> &list, Command<T> cmd) {
        if(cmd.end < cmd.start) cmd.end = cmd.start;
        list.push_back(cmd);
        this->start = std::min(this->start, cmd.start);
        this->end = std::max(this->end, cmd.end);
    }

    void sort() {
        const auto byTime = [](auto &list) {
            Mc::ranges::stable_sort(list, [](const auto &a, const auto &b) {
                return a.start != b.start ? a.start < b.start : a.end < b.end;
            });
        };
        byTime(this->x), byTime(this->y), byTime(this->scale), byTime(this->rotation), byTime(this->alpha);
        byTime(this->vectorScale), byTime(this->colour), byTime(this->flipH), byTime(this->flipV);
        byTime(this->additive);
    }
};

// commands repeated `iterations` times after `start`
struct Loop {
    f64 start;
    i32 iterations;
    Group cmds{};

    [[nodiscard]] f64 duration() const { return this->cmds.end - this->cmds.start; }
};

struct TriggerCondition {
    enum class Kind : u8 { PASSING, FAILING, HIT_OBJECT_HIT, HIT_SOUND };
    Kind kind;

    // filters for HIT_SOUND
    std::optional<u8> normalSet{};
    std::optional<u8> additionSet{};
    u8 additions{0};  // any of these (0: no filter)
    std::optional<i32> index{};

    [[nodiscard]] bool matches(const Storyboard::HitSound &sound) const {
        if(this->normalSet && sound.normalSet != *this->normalSet) return false;
        if(this->additions != 0 && (sound.additions & this->additions) == 0) return false;
        if(this->additionSet && (sound.additions == 0 || sound.additionSet != *this->additionSet)) return false;
        if(this->index && (i32)sound.index != *this->index) return false;
        return true;
    }
};

// commands executed when `condition` matches between `start` and `end`
struct Trigger {
    TriggerCondition condition;
    f64 start;
    f64 end;
    i32 groupNumber;  // (0: none) firing one stops others in the same group
    Group cmds{};
};

struct Sprite {
    Layer layer;
    vec2 origin;
    vec2 pos;
    std::string path;
    u32 frameCount{0};
    f64 frameDelay{0.0};
    bool loopForever{true};
    Group cmds{};
    std::vector<Loop> loops{};
    std::vector<Trigger> triggers{};

    f64 start{0.0};
    f64 end{0.0};
    f64 earliestCommand{0.0};
    u32 firstImage{0};

    // a parameter with a zero-length P of its kind anywhere (own commands, loops, triggers) is on until one applies
    bool instantFlipH{false};
    bool instantFlipV{false};
    bool instantAdditive{false};
};

struct Sample {
    f64 time;
    Layer layer;
    std::string path;
    f32 volume;  // 0 to 1
    u32 sound{0};
};

struct FiredTrigger {
    u32 trigger;
    f64 time;
};

template <typename T>
struct Latest {
    const Command<T> *cmd{nullptr};
    f64 offset{0.0};
    f64 start{-INF};

    bool consider(const std::vector<Command<T>> &cmds, f64 offset, f64 time) {
        const auto it = Mc::ranges::upper_bound(cmds, time - offset, {}, &Command<T>::start);
        if(it == cmds.begin()) return false;
        const Command<T> &cmd = *std::prev(it);
        if(cmd.start + offset >= this->start) {
            this->cmd = &cmd;
            this->offset = offset;
            this->start = cmd.start + offset;
        }
        return true;
    }
};

// a finite number (generators write NaN for values they couldn't work out)
template <typename T>
bool readNumber(std::string_view s, T &out) {
    return Parsing::strto_s(s, out) && std::isfinite(out);
}

// extra generous path handling, as it needs to be for some storyboards
// TODO: this logic is duplicated in various ways in multiple places
std::string cleanPath(std::string_view s) {
    s = SString::trimmed(s);
    if(s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    std::string path{s};
    File::normalizeSlashes(path, '\\', '/');
    return path;
}

std::optional<Layer> parseLayer(std::string_view s) {
    s = SString::trimmed(s);
    static constexpr std::array<std::pair<std::string_view, Layer>, 5> names{{
        {"Background", Layer::BACKGROUND},
        {"Fail", Layer::FAIL},
        {"Pass", Layer::PASS},
        {"Foreground", Layer::FOREGROUND},
        {"Overlay", Layer::OVERLAY},
    }};
    for(const auto &[name, layer] : names) {
        if(SString::strcase_equal(s, name)) return layer;
    }
    if(i32 n; Parsing::strto_s(s, n) && n >= 0 && n < (i32)Layer::COUNT) return (Layer)n;
    return std::nullopt;
}

// where on the image its position is, as a fraction of its size
std::optional<vec2> parseOrigin(std::string_view s) {
    s = SString::trimmed(s);
    static constexpr std::array<std::pair<std::string_view, vec2>, 10> names{{
        {"TopLeft", {0.f, 0.f}},
        {"Centre", {0.5f, 0.5f}},
        {"CentreLeft", {0.f, 0.5f}},
        {"TopRight", {1.f, 0.f}},
        {"BottomCentre", {0.5f, 1.f}},
        {"TopCentre", {0.5f, 0.f}},
        {"Custom", {0.f, 0.f}},  // TopLeft
        {"CentreRight", {1.f, 0.5f}},
        {"BottomLeft", {0.f, 1.f}},
        {"BottomRight", {1.f, 1.f}},
    }};
    for(const auto &[name, origin] : names) {
        if(SString::strcase_equal(s, name)) return origin;
    }
    if(i32 n; Parsing::strto_s(s, n) && n >= 0 && n < (i32)names.size()) return names[n].second;
    return std::nullopt;
}

// HitSound[set][set][addition][index]
// see https://osu.ppy.sh/wiki/en/Storyboard/Scripting/Compound_Commands
std::optional<TriggerCondition> parseTriggerCondition(std::string_view s) {
    using Kind = TriggerCondition::Kind;
    s = SString::trimmed(s);
    if(s == "Passing") return TriggerCondition{.kind = Kind::PASSING};
    if(s == "Failing") return TriggerCondition{.kind = Kind::FAILING};
    if(s == "HitObjectHit") return TriggerCondition{.kind = Kind::HIT_OBJECT_HIT};

    constexpr std::string_view prefix{"HitSound"};
    if(s.size() < prefix.size() || !SString::strcase_equal(s.substr(0, prefix.size()), prefix)) return std::nullopt;
    s.remove_prefix(prefix.size());

    // take the next word of `words` that is at the start of `s`
    const auto take = [&s](std::span<const std::string_view> words) -> std::optional<u8> {
        for(uSz i = 0; i < words.size(); i++) {
            if(s.size() >= words[i].size() && SString::strcase_equal(s.substr(0, words[i].size()), words[i])) {
                s.remove_prefix(words[i].size());
                return (u8)i;
            }
        }
        return std::nullopt;
    };

    static constexpr std::array<std::string_view, 4> sets{"All", "Normal", "Soft", "Drum"};
    static constexpr std::array<std::string_view, 3> additions{"Whistle", "Finish", "Clap"};
    const auto set1 = take(sets);
    const auto set2 = set1 ? take(sets) : std::nullopt;
    const auto addition = take(additions);

    TriggerCondition condition{.kind = Kind::HIT_SOUND};
    if(i32 index; !s.empty()) {
        if(!Parsing::strto_s(s, index)) return std::nullopt;
        condition.index = index;
    }

    // with only one set and an addition, the set is the addition's
    const bool set1IsAddition = set1 && !set2 && addition;
    // "All" == no filter
    const auto filter = [](std::optional<u8> set) {
        return set && *set > 0 ? std::optional<u8>{(u8)(*set - 1)} : std::nullopt;
    };
    condition.normalSet = filter(set1IsAddition ? set2 : set1);
    condition.additionSet = filter(set1IsAddition ? set1 : set2);
    condition.additions = addition ? (u8)(1 << *addition) : (u8)0;
    return condition;
}

// get all files of a folder, recursively
// TODO: implement recursive directory listing in File::getDirectoryEntries directly instead of reimplementing it everywhere
Hash::unstable_ncase_stringmap<std::string> listFolder(std::string_view folder) {
    Hash::unstable_ncase_stringmap<std::string> files;
    std::vector<std::pair<std::string, int>> pending{{"", 0}};
    std::vector<File::DirEntry> entries;
    while(!pending.empty()) {
        auto [dir, depth] = std::move(pending.back());
        pending.pop_back();
        entries.clear();
        if(!File::getDirectoryEntries(fmt::format("{}{}", folder, dir), File::DirContents::ALL, entries, false)) {
            continue;
        }
        for(auto &entry : entries) {
            std::string path = dir + entry.name;
            if(entry.type == File::FILETYPE::FOLDER) {
                // storyboards nest their assets a few folders deep, but no deeper than this
                if(depth < 8) pending.emplace_back(path + '/', depth + 1);
            } else {
                files.emplace(path, path);
            }
        }
    }
    return files;
}

}  // namespace

struct Storyboard::Impl {
    std::string folder;  // with a trailing slash
    std::string backgroundFile;
    vec2 backgroundOffset{0.f, 0.f};
    bool widescreen{false};
    bool useSkinSprites{false};

    std::vector<Sprite> sprites;
    std::array<std::vector<u32>, (uSz)Layer::COUNT> layers;  // sprites of each layer, in drawing order
    std::vector<Sample> samples;                             // sorted by time
    MD5Hash osbHash;
    bool hasOsb{false};

    // how long a play shows the storyboard: every command line's start and end as written (loop and trigger commands
    // at their relative times, without repetitions) and every sample
    f64 spanStart{INF};
    f64 spanEnd{-INF};

    // for loading
    struct ImageFile {
        std::string path;
        f32 scale;  // (0.5 for a skin's @2x image)
    };
    std::vector<ImageFile> imageFiles;
    std::vector<u32> frameFiles;
    std::vector<std::string> sampleFiles;
    std::vector<std::pair<std::string, std::string>> used;

    // once loaded
    struct Frame {
        const Image *image;
        f32 scale;
    };
    std::vector<Frame> frames;
    std::vector<Image *> images;
    std::vector<Sound *> sounds;
    std::optional<Async::Future<MD5Hash>> hashing;
    std::optional<MD5Hash> hash;

    // once played
    f64 time{-INF};
    bool passing{true};
    std::vector<std::vector<FiredTrigger>> fired;

    struct Parser;
    void finish();
    void resolveFiles(std::span<const std::string> skinDirs);

    template <typename T, typename Get>
    [[nodiscard]] T evaluate(u32 index, Get get, T fallback, f64 time) const;
    [[nodiscard]] std::optional<SpriteState> stateAt(u32 index, f64 time) const;

    void fire(TriggerCondition::Kind kind, const HitSound *sound, f64 time);
};

// reads a storyboard's files into it, the .osu's first (its variables carry over to the .osb)
struct Storyboard::Impl::Parser {
    Impl &sb;
    std::vector<std::pair<std::string, std::string>> variables{};
    i32 version{14};
    // where the next command goes: a sprite's own commands, one of its loops or one of its triggers
    Sprite *sprite{nullptr};
    Group *group{nullptr};
    std::vector<std::string_view> fields{};

    void file(std::string_view file, bool isOsu);
    void eventLine(std::string_view line);
};

void Storyboard::Impl::Parser::file(std::string_view file, bool isOsu) {
    const BeatmapFile osu{file};
    // .osb files have no version, they're read as the latest one
    this->version = isOsu ? osu.getVersion().value_or(14) : 14;
    this->sprite = nullptr;
    this->group = nullptr;

    auto &variables = this->variables;
    for(const auto &section : osu.getSections()) {
        const bool isVariables = section.name == "Variables";
        if(!isVariables && section.readAs != BeatmapFile::SectionKind::EVENTS &&
           section.readAs != BeatmapFile::SectionKind::GENERAL) {
            continue;
        }

        for(std::string_view line : SString::split_newlines(section.body)) {
            if(line.ends_with('\r')) line.remove_suffix(1);
            if(SString::is_wspace_only(line) || SString::is_comment(line)) continue;

            if(isVariables) {
                const uSz eq = line.find('=');
                if(eq == std::string_view::npos) continue;
                const std::string_view name = SString::trimmed(line.substr(0, eq));
                if(!name.starts_with('$')) continue;
                std::string value{SString::trimmed(line.substr(eq + 1))};
                if(auto it = Mc::ranges::find(variables, name, &std::pair<std::string, std::string>::first);
                   it != variables.end()) {
                    it->second = std::move(value);
                } else {
                    variables.emplace_back(name, std::move(value));
                }
            } else if(section.readAs == BeatmapFile::SectionKind::GENERAL) {
                if(BeatmapFile::KeyValue kv; BeatmapFile::parse(line, kv)) {
                    if(kv.key == "UseSkinSprites") this->sb.useSkinSprites = kv.value == "1";
                    if(kv.key == "WidescreenStoryboard") this->sb.widescreen = kv.value == "1";
                }
            } else if(line.contains('$') && !variables.empty()) {
                std::string substituted{line};
                for(const auto &[name, value] : variables) {
                    for(uSz pos = 0; (pos = substituted.find(name, pos)) != std::string::npos; pos += value.size()) {
                        substituted.replace(pos, name.size(), value);
                    }
                }
                this->eventLine(substituted);
            } else {
                this->eventLine(line);
            }
        }
    }
}

// absolute hell
void Storyboard::Impl::Parser::eventLine(std::string_view line) {
    auto &fields = this->fields;
    // strip out comments from commands
    if(const uSz comment = line.find("//"); comment != std::string_view::npos) line = line.substr(0, comment);

    uSz depth = 0;
    while(depth < line.size() && (line[depth] == ' ' || line[depth] == '_')) depth++;
    line.remove_prefix(depth);

    SString::split(fields, line, ',');
    if(fields.empty()) return;

    if(depth == 0) {
        this->sprite = nullptr;
        this->group = nullptr;

        const std::string_view type = SString::trimmed(fields[0]);
        if(type == "Background" || type == "0") {
            if(f32 x, y; fields.size() > 4 && readNumber(fields[3], x) && readNumber(fields[4], y)) {
                this->sb.backgroundOffset = {x, y};
            }
        } else if(type == "Sprite" || type == "4" || type == "Animation" || type == "6") {
            const bool animation = type == "Animation" || type == "6";
            if(fields.size() < (animation ? 8u : 6u)) return;
            const auto layer = parseLayer(fields[1]);
            const auto origin = parseOrigin(fields[2]);
            f32 x, y;
            if(!layer || !origin || !readNumber(fields[4], x) || !readNumber(fields[5], y)) return;

            Sprite sprite{.layer = *layer, .origin = *origin, .pos = {x, y}, .path = cleanPath(fields[3])};
            if(animation) {
                i32 frameCount;
                f64 frameDelay;
                if(!Parsing::strto_s(fields[6], frameCount) || !readNumber(fields[7], frameDelay) || frameCount <= 0) {
                    return;
                }
                if(this->version < 6) frameDelay = std::round(0.015 * frameDelay) * 1.186 * (1000.0 / 60.0);
                sprite.frameCount = (u32)frameCount;
                sprite.frameDelay = frameDelay;
                sprite.loopForever = fields.size() <= 8 ||
                                     (SString::trimmed(fields[8]) != "LoopOnce" && SString::trimmed(fields[8]) != "1");
            }
            this->sb.sprites.push_back(std::move(sprite));
            this->sprite = &this->sb.sprites.back();
        } else if(type == "Sample" || type == "5") {
            f64 time;
            const auto layer = fields.size() >= 4 ? parseLayer(fields[2]) : std::nullopt;
            if(!layer || !readNumber(fields[1], time)) return;
            f32 volume = 100.f;
            if(fields.size() > 4) (void)readNumber(fields[4], volume);
            this->sb.samples.push_back({.time = time,
                                        .layer = *layer,
                                        .path = cleanPath(fields[3]),
                                        .volume = std::clamp(volume, 0.f, 100.f) / 100.f});
            this->sb.spanStart = std::min(this->sb.spanStart, time);
            this->sb.spanEnd = std::max(this->sb.spanEnd, time);
        }
        return;
    }

    if(!this->sprite) return;
    if(depth < 2) this->group = &this->sprite->cmds;

    const std::string_view type = SString::trimmed(fields[0]);
    if(type == "L") {
        f64 start;
        i32 count;
        if(fields.size() < 3 || !readNumber(fields[1], start) || !Parsing::strto_s(fields[2], count)) return;
        this->sprite->loops.push_back({.start = start, .iterations = std::max(count, 1), .cmds = {}});
        this->group = &this->sprite->loops.back().cmds;
        return;
    }
    if(type == "T") {
        const auto condition = fields.size() >= 2 ? parseTriggerCondition(fields[1]) : std::nullopt;
        if(!condition) {
            this->group = nullptr;
            return;
        }
        f64 start = -INF, end = INF;
        i32 groupNumber = 0;
        if(fields.size() > 2) (void)readNumber(fields[2], start);
        if(fields.size() > 3) (void)readNumber(fields[3], end);
        if(fields.size() > 4) (void)Parsing::strto_s(fields[4], groupNumber);
        this->sprite->triggers.push_back(
            {.condition = *condition, .start = start, .end = end, .groupNumber = groupNumber, .cmds = {}});
        this->group = &this->sprite->triggers.back().cmds;
        return;
    }

    Group *group = this->group;
    if(!group || fields.size() < 5) return;

    i32 easing;
    f64 start, end;
    if(!Parsing::strto_s(fields[1], easing) || !readNumber(fields[2], start)) return;
    if(SString::trimmed(fields[3]).empty()) {
        end = start;
    } else if(!readNumber(fields[3], end)) {
        return;
    }
    this->sb.spanStart = std::min(this->sb.spanStart, start);
    this->sb.spanEnd = std::max(this->sb.spanEnd, end);
    const Ease ease = easing >= 0 && easing < (i32)Ease::COUNT ? (Ease)easing : Ease::Linear;

    if(type == "P") {
        const std::string_view param = SString::trimmed(fields[4]);
        const Command<bool> cmd{.start = start, .end = end, .from = true, .to = start == end, .easing = ease};
        if(param == "H") {
            group->add(group->flipH, cmd);
            this->sprite->instantFlipH |= start == end;
        } else if(param == "V") {
            group->add(group->flipV, cmd);
            this->sprite->instantFlipV |= start == end;
        } else if(param == "A") {
            group->add(group->additive, cmd);
            this->sprite->instantAdditive |= start == end;
        }
        return;
    }

    const auto values = std::span{fields}.subspan(4);
    const auto commands = [&]<typename T>(std::vector<Command<T>> &list, uSz arity, auto read) {
        T from;
        if(values.size() < arity || !read(values.subspan(0, arity), from)) return;

        // https://osu.ppy.sh/wiki/en/Storyboard/Scripting/Shorthand
        const f64 duration = std::max(end - start, 0.0);
        const uSz before = list.size();
        const f64 groupStart = group->start, groupEnd = group->end;
        for(uSz i = arity; i + arity <= values.size(); i += arity) {
            T to;
            if(!read(values.subspan(i, arity), to)) {
                // (one invalid value drops the whole line)
                list.resize(before);
                group->start = groupStart, group->end = groupEnd;
                return;
            }
            const f64 offset = duration * (f64)(i / arity - 1);
            group->add(
                list, Command<T>{.start = start + offset, .end = end + offset, .from = from, .to = to, .easing = ease});
            from = to;
        }
        if(list.size() == before) {
            group->add(list, Command<T>{.start = start, .end = end, .from = from, .to = from, .easing = ease});
        }
    };
    const auto one = [](std::span<const std::string_view> v, f32 &out) { return readNumber(v[0], out); };
    const auto two = [](std::span<const std::string_view> v, vec2 &out) {
        return readNumber(v[0], out.x) && readNumber(v[1], out.y);
    };

    if(type == "F") {
        commands(group->alpha, 1, one);
    } else if(type == "S") {
        commands(group->scale, 1, one);
    } else if(type == "R") {
        commands(group->rotation, 1, one);
    } else if(type == "MX") {
        commands(group->x, 1, one);
    } else if(type == "MY") {
        commands(group->y, 1, one);
    } else if(type == "V") {
        commands(group->vectorScale, 2, two);
    } else if(type == "M") {
        std::vector<Command<vec2>> moves;
        commands(moves, 2, two);
        for(const auto &m : moves) {
            group->add(group->x, Command<f32>{m.start, m.end, m.from.x, m.to.x, m.easing});
            group->add(group->y, Command<f32>{m.start, m.end, m.from.y, m.to.y, m.easing});
        }
    } else if(type == "C") {
        commands(group->colour, 3, [](std::span<const std::string_view> v, vec3 &out) {
            if(!readNumber(v[0], out.x) || !readNumber(v[1], out.y) || !readNumber(v[2], out.z)) return false;
            out /= 255.f;
            return true;
        });
    }
}

void Storyboard::Impl::finish() {
    // only what has something to do is drawn
    std::erase_if(this->sprites, [](Sprite &sprite) {
        std::erase_if(sprite.loops, [](const Loop &loop) { return loop.cmds.empty(); });
        return sprite.cmds.empty() && sprite.loops.empty();
    });

    for(u32 i = 0; i < this->sprites.size(); i++) {
        Sprite &sprite = this->sprites[i];
        sprite.cmds.sort();
        for(auto &loop : sprite.loops) loop.cmds.sort();
        for(auto &trigger : sprite.triggers) trigger.cmds.sort();

        f64 earliest = sprite.cmds.start;
        f64 end = sprite.cmds.end;
        for(const auto &loop : sprite.loops) {
            earliest = std::min(earliest, loop.start + loop.cmds.start);
            end = std::max(end, loop.start + loop.cmds.start + loop.duration() * loop.iterations);
        }
        sprite.earliestCommand = earliest;
        sprite.start = earliest;

        std::vector<std::pair<f64, const Command<f32> *>> alphas;  // (startTime, cmd)
        const auto collectAlphas = [&alphas](const std::vector<Command<f32>> &cmds, f64 offset) {
            for(const auto &cmd : cmds) {
                alphas.emplace_back(cmd.start + offset, &cmd);
                if(cmd.from > 0.f || cmd.to > 0.f) break;
            }
        };
        collectAlphas(sprite.cmds.alpha, 0.0);
        for(const auto &loop : sprite.loops) collectAlphas(loop.cmds.alpha, loop.start);
        if(!alphas.empty()) {
            const auto first = Mc::ranges::min_element(alphas, {}, &std::pair<f64, const Command<f32> *>::first);
            f64 firstVisible = INF;
            for(const auto &[start, cmd] : alphas) {
                if(cmd->from > 0.f || cmd->to > 0.f) firstVisible = std::min(firstVisible, start);
            }
            if(first->second->from == 0.f && firstVisible < INF) sprite.start = firstVisible;
        }
        sprite.end = end;

        this->layers[(uSz)sprite.layer].push_back(i);
    }

    Mc::ranges::stable_sort(this->samples, {}, &Sample::time);
    this->fired.resize(this->sprites.size());
}

void Storyboard::Impl::resolveFiles(std::span<const std::string> skinDirs) {
    const auto files = this->folder.empty() ? Hash::unstable_ncase_stringmap<std::string>{} : listFolder(this->folder);

    // the set's file a script names (case-insensitive), else the name with one of `extensions` instead of its own:
    // for a name without one (old storyboards leave it out), or for any name that isn't there with `replace`
    const auto resolve = [&files](std::string_view path, std::span<const std::string_view> extensions,
                                  bool replace) -> std::string {
        if(path.empty() || path.starts_with('/') ||
           Mc::ranges::any_of(SString::split(path, '/'), [](std::string_view part) { return part == ".."; })) {
            return {};
        }
        if(const auto it = files.find(path); it != files.end()) return it->second;

        const uSz dot = path.rfind('.');
        const bool hasExtension = dot != std::string_view::npos && !path.substr(dot).contains('/');
        if(hasExtension && !replace) return {};
        const std::string_view stem = hasExtension ? path.substr(0, dot) : path;
        for(const std::string_view ext : extensions) {
            if(const auto it = files.find(fmt::format("{}{}", stem, ext)); it != files.end()) return it->second;
        }
        return {};
    };
    static constexpr std::array<std::string_view, 3> imageExtensions{".png", ".jpg", ".jpeg"};
    static constexpr std::array<std::string_view, 3> sampleExtensions{".wav", ".ogg", ".mp3"};

    // the set's image, else with UseSkinSprites the skin's as the skin loads its own elements (an @2x one first)
    const auto findSkinImage = [&](const std::string &path) -> std::optional<ImageFile> {
        if(!this->useSkinSprites) return std::nullopt;
        const std::string name = path.substr(path.rfind('/') + 1).contains('.') ? path : path + ".png";
        const uSz dot = name.rfind('.');
        for(const auto &dir : skinDirs) {
            if(std::string hd = fmt::format("{}{}@2x{}", dir, std::string_view{name}.substr(0, dot),
                                            std::string_view{name}.substr(dot));
               cv::skin_hd.getBool() && File::existsCaseInsensitive(hd) == File::FILETYPE::FILE) {
                return ImageFile{.path = std::move(hd), .scale = 0.5f};
            }
            if(std::string file = dir + name; File::existsCaseInsensitive(file) == File::FILETYPE::FILE) {
                return ImageFile{.path = std::move(file), .scale = 1.f};
            }
        }
        return std::nullopt;
    };

    Hash::unstable_ncase_stringmap<u32> knownImages;
    const auto findImage = [&](const std::string &path) -> u32 {
        if(const auto it = knownImages.find(path); it != knownImages.end()) return it->second;
        const std::string inFolder = resolve(path, imageExtensions, false);
        this->used.emplace_back(SString::to_lower(path), inFolder);
        auto file = inFolder.empty() ? findSkinImage(path) : ImageFile{.path = this->folder + inFolder, .scale = 1.f};

        u32 index = NO_FILE;
        if(file) {
            index = (u32)this->imageFiles.size();
            this->imageFiles.push_back(std::move(*file));
        }
        knownImages.emplace(path, index);
        return index;
    };

    for(auto &sprite : this->sprites) {
        sprite.firstImage = (u32)this->frameFiles.size();
        if(sprite.frameCount == 0) {
            this->frameFiles.push_back(findImage(sprite.path));
            continue;
        }
        const uSz slash = sprite.path.rfind('/');
        const uSz dot = sprite.path.rfind('.');
        const uSz numberAt =
            dot != std::string::npos && (slash == std::string::npos || dot > slash) ? dot : sprite.path.size();
        for(u32 i = 0; i < sprite.frameCount; i++) {
            std::string framePath = sprite.path;
            framePath.insert(numberAt, std::to_string(i));
            this->frameFiles.push_back(findImage(framePath));
        }
    }

    // (drawn from the map's own background, but part of what the storyboard shows)
    if(!this->backgroundFile.empty()) {
        this->used.emplace_back(SString::to_lower(this->backgroundFile),
                                resolve(this->backgroundFile, imageExtensions, false));
    }

    Hash::unstable_ncase_stringmap<u32> knownSamples;
    for(auto &sample : this->samples) {
        if(const auto it = knownSamples.find(sample.path); it != knownSamples.end()) {
            sample.sound = it->second;
            continue;
        }
        std::string inFolder = resolve(sample.path, sampleExtensions, true);
        this->used.emplace_back(SString::to_lower(sample.path), inFolder);
        sample.sound = (u32)this->sampleFiles.size();
        this->sampleFiles.push_back(inFolder.empty() ? std::string{} : this->folder + inFolder);
        knownSamples.emplace(sample.path, sample.sound);
    }
}

template <typename T, typename Get>
T Storyboard::Impl::evaluate(u32 index, Get get, T fallback, f64 time) const {
    const Sprite &sprite = this->sprites[index];
    Latest<T> latest;
    latest.consider(get(sprite.cmds), 0.0, time);
    for(const auto &loop : sprite.loops) {
        const auto &cmds = get(loop.cmds);
        if(cmds.empty()) continue;
        const f64 duration = loop.duration();
        i32 iteration = 0;
        if(duration > 0.0) {
            iteration = (i32)std::clamp(std::floor((time - loop.start - loop.cmds.start) / duration), 0.0,
                                        (f64)(loop.iterations - 1));
        }
        if(!latest.consider(cmds, loop.start + duration * iteration, time) && iteration > 0) {
            latest.consider(cmds, loop.start + duration * (iteration - 1), time);
        }
    }
    for(const auto &fired : this->fired[index]) {
        latest.consider(get(sprite.triggers[fired.trigger].cmds), fired.time, time);
    }

    if(latest.cmd) {
        Command<T> cmd = *latest.cmd;
        cmd.start += latest.offset;
        cmd.end += latest.offset;

        if constexpr(std::is_same_v<T, bool>) {
            return time < cmd.end ? cmd.from : cmd.to;
        } else {
            if(time >= cmd.end || cmd.end <= cmd.start) return cmd.to;
            const f32 progress = AnimationHandler::ease(cmd.easing, (time - cmd.start) / (cmd.end - cmd.start));
            return cmd.from + (cmd.to - cmd.from) * progress;
        }
    }

    const Command<T> *first = nullptr;
    f64 firstStart = INF;
    if(const auto &cmds = get(sprite.cmds); !cmds.empty()) {
        first = &cmds.front();
        firstStart = cmds.front().start;
    }
    for(const auto &loop : sprite.loops) {
        if(const auto &cmds = get(loop.cmds); !cmds.empty() && loop.start + cmds.front().start < firstStart) {
            first = &cmds.front();
            firstStart = loop.start + cmds.front().start;
        }
    }
    if(!first) return fallback;
    if constexpr(std::is_same_v<T, bool>) {
        return first->start == first->end;
    } else {
        return first->from;
    }
}

std::optional<Storyboard::SpriteState> Storyboard::Impl::stateAt(u32 index, f64 time) const {
    const Sprite &sprite = this->sprites[index];
    if(time < sprite.start || time >= sprite.end) return std::nullopt;

    // clang-format off
    SpriteState state{
        .pos = {this->evaluate<f32>(index, [](const Group &g) -> auto & { return g.x; }, sprite.pos.x, time),
                this->evaluate<f32>(index, [](const Group &g) -> auto & { return g.y; }, sprite.pos.y, time)},
        .scale = this->evaluate<vec2>(index, [](const Group &g) -> auto & { return g.vectorScale; }, vec2{1.f}, time) *
                 this->evaluate<f32>(index, [](const Group &g) -> auto & { return g.scale; }, 1.f, time),
        .rotation = this->evaluate<f32>(index, [](const Group &g) -> auto & { return g.rotation; }, 0.f, time),
        .colour = this->evaluate<vec3>(index, [](const Group &g) -> auto & { return g.colour; }, vec3{1.f}, time),
        .alpha = this->evaluate<f32>(index, [](const Group &g) -> auto & { return g.alpha; }, 1.f, time),
        .flipH = this->evaluate<bool>(index, [](const Group &g) -> auto & { return g.flipH; }, false, time),
        .flipV = this->evaluate<bool>(index, [](const Group &g) -> auto & { return g.flipV; }, false, time),
        .additive = this->evaluate<bool>(index, [](const Group &g) -> auto & { return g.additive; }, false, time),
        .frame = 0,
    };
    // clang-format on

    // an opacity above 1 wraps around, which storyboards use for flickering
    if(state.alpha > 1.f) state.alpha = std::fmod(state.alpha, 1.f);

    if(sprite.frameCount > 0 && sprite.frameDelay > 0.0) {
        const auto frame = (i64)std::floor((time - sprite.earliestCommand) / sprite.frameDelay);
        const auto count = (i64)sprite.frameCount;
        state.frame =
            (u32)(sprite.loopForever ? ((frame % count) + count) % count : std::clamp<i64>(frame, 0, count - 1));
    }
    return state;
}

void Storyboard::Impl::fire(TriggerCondition::Kind kind, const HitSound *sound, f64 time) {
    for(u32 s = 0; s < this->sprites.size(); s++) {
        const Sprite &sprite = this->sprites[s];
        if(sprite.triggers.empty() || time < sprite.start || time >= sprite.end) continue;
        for(u32 t = 0; t < sprite.triggers.size(); t++) {
            const Trigger &trigger = sprite.triggers[t];
            if(trigger.condition.kind != kind || time < trigger.start || time > trigger.end) continue;
            if(sound && !trigger.condition.matches(*sound)) continue;

            // a trigger that fires again starts over, and stops others of the same group
            auto &fired = this->fired[s];
            std::erase_if(fired, [&](const FiredTrigger &f) {
                return f.trigger == t ||
                       (trigger.groupNumber != 0 && sprite.triggers[f.trigger].groupNumber == trigger.groupNumber);
            });
            fired.push_back({.trigger = t, .time = time});
        }
    }
}

Storyboard::Storyboard(std::string folder, std::string_view osuFile, std::string_view osbFile,
                       std::string backgroundFile, std::span<const std::string> skinDirs)
    : impl() {
    this->impl->folder = std::move(folder);
    if(!this->impl->folder.empty() && !this->impl->folder.ends_with('/')) this->impl->folder.push_back('/');
    this->impl->backgroundFile = std::move(backgroundFile);
    File::normalizeSlashes(this->impl->backgroundFile, '\\', '/');

    Impl::Parser parser{.sb = *this->impl};
    parser.file(osuFile, true);
    if(!osbFile.empty()) {
        this->impl->hasOsb = true;
        this->impl->osbHash = crypto::hash::md5(osbFile);
        parser.file(osbFile, false);
    }
    this->impl->finish();
    this->impl->resolveFiles(skinDirs);
}

Storyboard::~Storyboard() {
    for(Image *image : this->impl->images) {
        if(image) resourceManager->destroyResource(image);
    }
    for(Sound *sound : this->impl->sounds) {
        if(sound) resourceManager->destroyResource(sound);
    }
}

Async::CancellableHandle<std::unique_ptr<Storyboard>> Storyboard::parseAsync(
    const DatabaseBeatmap &map, std::shared_ptr<const std::vector<u8>> osuFile, std::vector<std::string> skinDirs,
    Lane lane) {
    return Async::submit_cancellable(
        [folder = std::string{map.getFolder()}, osuPath = std::string{map.getFilePath()},
         artist = std::string{map.getArtistLatin()}, title = std::string{map.getTitleLatin()},
         creator = std::string{map.getCreator()}, background = std::string{map.getBackgroundImageFileName()},
         osuFile = std::move(osuFile),
         skinDirs = std::move(skinDirs)](const Sync::stop_token &tok) -> std::unique_ptr<Storyboard> {
            std::string osuRead;
            if(!osuFile) {
                File file(osuPath);
                if(!file.canRead()) return nullptr;
                osuRead = file.readToString();
            }
            const std::string_view osu =
                osuFile ? std::string_view{reinterpret_cast<const char *>(osuFile->data()), osuFile->size()}
                        : std::string_view{osuRead};

            std::string osb;
            if(const std::string osbFile = findOsbFile(folder, artist, title, creator); !osbFile.empty()) {
                File file(folder + osbFile);
                if(file.canRead()) osb = file.readToString();
            }
            if(tok.stop_requested()) return nullptr;

            auto storyboard = std::make_unique<Storyboard>(folder, osu, osb, background, skinDirs);
            if(storyboard->isEmpty()) return nullptr;
            return storyboard;
        },
        lane);
}

std::string Storyboard::findOsbFile(std::string_view folder, std::string_view artist, std::string_view title,
                                    std::string_view creator) {
    std::vector<std::string> osbs;
    for(auto &file : Environment::getFilesInFolder(folder)) {
        if(file.size() > 4 && SString::strcase_equal(std::string_view{file}.substr(file.size() - 4), ".osb")) {
            osbs.push_back(std::move(file));
        }
    }
    if(osbs.empty()) return {};
    const std::string expected = fmt::format("{} - {} ({}).osb", artist, title, creator);
    if(const auto it = Mc::ranges::find_if(osbs, [&](const auto &f) { return SString::strcase_equal(f, expected); });
       it != osbs.end()) {
        return *it;
    }
    return *Mc::ranges::min_element(osbs);
}

bool Storyboard::isEmpty() const { return this->impl->sprites.empty() && this->impl->samples.empty(); }
vec2 Storyboard::getBackgroundOffset() const { return this->impl->backgroundOffset; }
uSz Storyboard::getNumSprites() const { return this->impl->sprites.size(); }
uSz Storyboard::getNumSamples() const { return this->impl->samples.size(); }

bool Storyboard::replacesBackground() const {
    const auto &bg = this->impl->backgroundFile;
    if(bg.empty()) return false;
    return Mc::ranges::any_of(this->impl->layers[(uSz)Layer::BACKGROUND],
                              [&](u32 i) { return SString::strcase_equal(this->impl->sprites[i].path, bg); });
}

std::optional<Storyboard::SpriteState> Storyboard::getSpriteState(uSz sprite, f64 time) const {
    if(sprite >= this->impl->sprites.size()) return std::nullopt;
    return this->impl->stateAt((u32)sprite, time);
}

std::pair<f64, f64> Storyboard::getSpriteSpan(uSz sprite) const {
    const Sprite &s = this->impl->sprites[sprite];
    return {s.start, s.end};
}

std::optional<f64> Storyboard::getEndTime() const {
    std::optional<f64> end;
    for(const auto &sprite : this->impl->sprites) end = std::max(end.value_or(-INF), sprite.end);
    for(const auto &sample : this->impl->samples) end = std::max(end.value_or(-INF), sample.time);
    return end;
}

std::string_view Storyboard::getImageFile(uSz sprite, u32 frame) const {
    const u32 file = this->impl->frameFiles[this->impl->sprites[sprite].firstImage + frame];
    return file == NO_FILE ? std::string_view{} : this->impl->imageFiles[file].path;
}

std::string_view Storyboard::getSampleFile(uSz sample) const {
    return this->impl->sampleFiles[this->impl->samples[sample].sound];
}

void Storyboard::load(bool visualsOnly) {
    Impl &sb = *this->impl;

    if(sb.frames.empty()) {
        sb.images.reserve(sb.imageFiles.size());
        for(const auto &file : sb.imageFiles) {
            resourceManager->requestNextLoadAsync();
            sb.images.push_back(resourceManager->loadImageAbsUnnamed(file.path));
        }
        sb.frames.reserve(sb.frameFiles.size());
        for(const u32 file : sb.frameFiles) {
            sb.frames.push_back(file == NO_FILE
                                    ? Impl::Frame{.image = nullptr, .scale = 1.f}
                                    : Impl::Frame{.image = sb.images[file], .scale = sb.imageFiles[file].scale});
        }
    }

    if(visualsOnly || sb.hashing || sb.hash) return;

    sb.sounds.reserve(sb.sampleFiles.size());
    for(const auto &file : sb.sampleFiles) {
        Sound *sound = nullptr;
        if(!file.empty()) {
            resourceManager->requestNextLoadAsync();
            sound = resourceManager->loadSoundAbs(file, "", false, true);
        }
        sb.sounds.push_back(sound);
    }

    // no idea how stable generates the storyboard hash, claude picked this.
    // i don't think this matters much, no server is going to extract .oszs
    // and do a bunch of parsing when they can just look at the most used hash
    // on the leaderboard.
    std::vector<std::pair<std::string, std::string>> used = std::move(sb.used);
    Mc::ranges::sort(used);
    used.erase(std::unique(used.begin(), used.end()), used.end());
    sb.hashing = Async::submit(
        [folder = sb.folder, used = std::move(used), osb = sb.hasOsb ? std::optional{sb.osbHash} : std::nullopt]() {
            std::string manifest = fmt::format("osb\t{}\n", osb ? osb->to_chars().string() : "-");
            for(const auto &[name, file] : used) {
                const auto md5 = file.empty() ? std::nullopt : crypto::hash::md5_file(folder + file);
                manifest += fmt::format("{}\t{}\n", name, md5 ? md5->to_chars().string() : "-");
            }
            return crypto::hash::md5(manifest);
        });
}

bool Storyboard::isReady() const {
    if(this->impl->hashing && !this->impl->hashing->is_ready()) return false;
    return Mc::ranges::none_of(this->impl->images,
                               [](const Image *i) { return i && resourceManager->isLoadingResource(i); }) &&
           Mc::ranges::none_of(this->impl->sounds,
                               [](const Sound *s) { return s && resourceManager->isLoadingResource(s); });
}

MD5Hash Storyboard::getHash() {
    Impl &sb = *this->impl;
    if(!sb.hash && sb.hashing) {
        sb.hash = sb.hashing->get();
        sb.hashing.reset();
    }
    return sb.hash.value_or(MD5Hash{});
}

void Storyboard::update(f64 time, bool passing, bool seeked) {
    Impl &sb = *this->impl;

    // (the first update is where it starts, not a move)
    seeked |= sb.time == -INF || time < sb.time;

    if(seeked) {
        for(auto &fired : sb.fired) fired.clear();
    } else if(time > sb.time) {
        for(auto it = sb.sounds.empty() ? sb.samples.end()
                                        : Mc::ranges::upper_bound(sb.samples, sb.time, {}, &Sample::time);
            it != sb.samples.end() && it->time <= time; ++it) {
            if((it->layer == Layer::FAIL && passing) || (it->layer == Layer::PASS && !passing)) continue;
            if(Sound *sound = sb.sounds[it->sound]; sound && sound->isReady()) {
                soundEngine->play(sound, 0.f, 0.f, it->volume * cv::volume_effects.getFloat());
            }
        }
        if(passing != sb.passing) {
            sb.fire(passing ? TriggerCondition::Kind::PASSING : TriggerCondition::Kind::FAILING, nullptr, time);
        }
    }

    sb.time = time;
    sb.passing = passing;
}

void Storyboard::onHitSound(HitSound sound, f64 time) {
    this->impl->fire(TriggerCondition::Kind::HIT_SOUND, &sound, time);
}

void Storyboard::onHitObjectHit(f64 time) { this->impl->fire(TriggerCondition::Kind::HIT_OBJECT_HIT, nullptr, time); }

void Storyboard::View::transform() const {
    g->translate(-this->pivot);
    g->scale(this->flipX ? -1.f : 1.f, this->flipY ? -1.f : 1.f);
    g->rotate(-this->rotation);
    g->translate(this->pivot);
}

void Storyboard::draw(Layer first, Layer last, const View &view) const {
    const Impl &sb = *this->impl;
    const McRect &screen = view.screen;

    // storyboards have a virtual 4:3 resolution of 640x480
    // but nothing stops a mapper from going further neg/pos to support 16:9
    const f32 scale = screen.getHeight() / 480.f;
    const vec2 topLeft{screen.getX() + (screen.getWidth() - 640.f * scale) / 2.f, screen.getY()};

    for(auto layer = (uSz)first; layer <= (uSz)last; layer++) {
        if(((Layer)layer == Layer::FAIL && sb.passing) || ((Layer)layer == Layer::PASS && !sb.passing)) continue;

        for(const u32 index : sb.layers[layer]) {
            const auto state = sb.stateAt(index, sb.time);
            if(!state || state->alpha <= 0.f || state->scale.x == 0.f || state->scale.y == 0.f) continue;
            const Sprite &sprite = sb.sprites[index];
            const auto [image, imageScale] = sb.frames[sprite.firstImage + state->frame];
            if(!image || !image->isReady()) continue;

            // a sprite is mirrored in place: where it is stays the same, flipped or with a negative scale
            const vec2 size{(f32)image->getWidth(), (f32)image->getHeight()};
            const vec2 originFromCenter = (sprite.origin - 0.5f) * size;
            const bool mirrorX = state->flipH != (state->scale.x < 0.f);
            const bool mirrorY = state->flipV != (state->scale.y < 0.f);

            const vec3 colour = state->colour * view.brightness;
            g->setColor(argb(state->alpha * view.alpha, colour.x, colour.y, colour.z));
            if(state->additive) g->setBlendMode(DrawBlendMode::ADDITIVE);
            g->pushTransform();
            {
                g->scale(mirrorX ? -1.f : 1.f, mirrorY ? -1.f : 1.f);
                g->translate(-originFromCenter);
                g->scale(std::abs(state->scale.x) * imageScale, std::abs(state->scale.y) * imageScale);
                g->rotate(glm::degrees(state->rotation));
                g->translate(state->pos);
                g->scale(scale, scale);
                g->translate(topLeft);
                view.transform();
                g->drawImage(image);
            }
            g->popTransform();
            if(state->additive) g->setBlendMode(DrawBlendMode::ALPHA);
        }
    }
}
