// Copyright (c) 2026, WH, All rights reserved.
#include "MapFiles.h"

#include "Document.h"
#include "Environment.h"
#include "File.h"
#include "SString.h"

#include "fmt/format.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

using Kind = neomod::BeatmapFile::SectionKind;

namespace MapFiles {
std::string nameFor(const neomod::BeatmapFile &file) {
    auto value = [&file](Kind kind, std::string_view key) { return file.getValue(kind, key).value_or(""); };
    std::string name;
    if(const std::string_view artist = value(Kind::METADATA, "Artist"); !artist.empty()) {
        name = fmt::format("{} - {}", artist, value(Kind::METADATA, "Title"));
    } else {
        const std::string_view audio = value(Kind::GENERAL, "AudioFilename");
        name = audio.substr(0, audio.rfind('.'));
    }
    if(const std::string_view creator = value(Kind::METADATA, "Creator"); !creator.empty()) {
        name += fmt::format(" ({})", creator);
    }
    if(const std::string_view version = value(Kind::METADATA, "Version"); !version.empty()) {
        name += fmt::format(" [{}]", version);
    }

    std::erase_if(name, [](char c) { return std::string_view{"\"<>|:*?\\/"}.contains(c) || (unsigned char)c < 32; });
    if(name.size() > 200) {
        // not inside a utf-8 sequence
        uSz cut = 200;
        while(cut > 0 && ((unsigned char)name[cut] & 0xC0) == 0x80) cut--;
        name.resize(cut);
    }
    return name + ".osu";
}

std::vector<u8> newDifficulty(std::span<const u8> content, std::string_view version, bool empty) {
    auto [document, error] = Document::read({}, "", content);
    if(!document) return {};
    std::vector<Document::Id> gone;
    if(empty) {
        for(const auto line : document->getFile().getEntries(Kind::HIT_OBJECTS)) {
            gone.push_back(document->getIdAt(line.number));
        }
        for(const Document::Break &b : document->getBreaks()) gone.push_back(b.id);
    }
    if(!document->change(Document::Action::METADATA,
                         [&](Document::Change &c) {
                             c.setValue(Kind::METADATA, "Version", version);
                             c.setValue(Kind::METADATA, "BeatmapID", "0");
                             c.remove(gone);
                         }) &&
       document->getError().errc) {
        return {};
    }
    return {document->getBytes().begin(), document->getBytes().end()};
}

std::string newSet(std::string_view audio, std::string_view creator) {
    const std::string_view title = audio.substr(0, audio.rfind('.'));
    std::string romanised;
    for(const char c : title) {
        if(c >= 32 && c <= 126) romanised += c;
    }
    SString::trim_inplace(romanised);
    return fmt::format(
        "osu file format v14\r\n"
        "\r\n"
        "[General]\r\n"
        "AudioFilename: {}\r\n"
        "AudioLeadIn: 0\r\n"
        "PreviewTime: -1\r\n"
        "Countdown: 1\r\n"
        "SampleSet: Normal\r\n"
        "StackLeniency: 0.7\r\n"
        "Mode: 0\r\n"
        "LetterboxInBreaks: 0\r\n"
        "WidescreenStoryboard: 0\r\n"
        "\r\n"
        "[Editor]\r\n"
        "BeatDivisor: 4\r\n"
        "\r\n"
        "[Metadata]\r\n"
        "Title:{}\r\n"
        "TitleUnicode:{}\r\n"
        "Artist:\r\n"
        "ArtistUnicode:\r\n"
        "Creator:{}\r\n"
        "Version:\r\n"
        "Source:\r\n"
        "Tags:\r\n"
        "BeatmapID:0\r\n"
        "BeatmapSetID:-1\r\n"
        "\r\n"
        "[Difficulty]\r\n"
        "HPDrainRate:5\r\n"
        "CircleSize:5\r\n"
        "OverallDifficulty:5\r\n"
        "ApproachRate:5\r\n"
        "SliderMultiplier:1.4\r\n"
        "SliderTickRate:1\r\n"
        "\r\n"
        "[Events]\r\n"
        "//Background and Video events\r\n"
        "//Break Periods\r\n"
        "//Storyboard Layer 0 (Background)\r\n"
        "//Storyboard Layer 1 (Fail)\r\n"
        "//Storyboard Layer 2 (Pass)\r\n"
        "//Storyboard Layer 3 (Foreground)\r\n"
        "//Storyboard Sound Samples\r\n"
        "\r\n"
        "[TimingPoints]\r\n"
        "0,500,4,1,0,100,1,0\r\n"
        "\r\n"
        "\r\n"
        "[HitObjects]\r\n",
        audio, romanised, title, creator);
}

std::optional<std::string> takeIn(const std::string &folder, const std::string &path) {
    std::string name = Environment::getFileNameFromFilePath(path);
    if(std::error_code ec; std::filesystem::equivalent(File::getFsPath(path), File::getFsPath(folder + name), ec)) {
        return name;
    }
    const uSz dot = std::min(name.rfind('.'), name.size());
    const std::string stem = name.substr(0, dot), extension = name.substr(dot);
    for(i32 n = 2; Environment::fileExists(folder + name); n++) name = fmt::format("{} ({}){}", stem, n, extension);
    if(!File::copy(path, folder + name)) return std::nullopt;
    return name;
}
}  // namespace MapFiles
