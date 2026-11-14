// Copyright (c) 2026, WH, All rights reserved.
#include "EditorSession.h"

#include "BanchoPacket.h"
#include "BeatSnap.h"
#include "ByteBufferedFile.h"
#include "ContainerRanges.h"
#include "Environment.h"
#include "File.h"
#include "Logging.h"
#include "Paths.h"
#include "Snapping.h"
#include "crypto.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {
// the format: its version, the open map (if any) with its draft (if any), the views, the tool, the selection and the
// tab (version 5 had no tab)
constexpr u8 SESSION_VERSION{6};
// in a draft file: its version, the map, the draft
constexpr u8 DRAFT_VERSION{1};

template <typename W>
void writeDraft(W &out, const EditorSession::Draft &draft) {
    out.write_hash_digest(draft.base);
    out.write((u32)draft.bytes.size());
    out.write_bytes(draft.bytes);
}

bool readDraft(PacketReader &in, EditorSession::Draft &draft) {
    draft.base = in.read_hash_digest();
    const auto bytes = in.read_span(in.read<u32>());
    draft.bytes.assign(bytes.begin(), bytes.end());
    return in.good() && !draft.bytes.empty();
}

// <editor>/drafts/<md5 for the location in maps/>: a map in the songs folder uses the one its copy has
// (Database::standIn), so that its draft belongs to the copy once its set was copied
std::string draftFile(const Database::MapLocation &location) {
    const Database::MapLocation in_maps = Database::standIn(location);
    return fmt::format("{}/drafts/{}", Mc::Paths::editor(),
                       crypto::hash::md5(fmt::format("{}/{}/{}", (int)in_maps.root, in_maps.folder, in_maps.file)));
}

template <typename W>
void writeLocation(W &out, const Database::MapLocation &location) {
    out.write((u8)location.root);
    out.write_string(location.folder);
    out.write_string(location.file);
}

bool readLocation(PacketReader &in, Database::MapLocation &location) {
    const u8 root = in.read<u8>();
    location.folder = in.read_string();
    location.file = in.read_string();
    location.root = (Database::MapRoot)root;
    return in.good() && root <= (u8)Database::MapRoot::Peppy && !location.file.empty();
}
}  // namespace

EditorSession EditorSession::read(std::span<const u8> bytes) {
    PacketReader in(bytes);
    EditorSession session;
    const u8 version = in.read<u8>();
    bool good = version >= 5 && version <= SESSION_VERSION;
    if(good && in.read<u8>() != 0) good = readLocation(in, session.open.emplace());
    if(good && in.read<u8>() != 0) good = session.open && readDraft(in, session.draft.emplace());
    const u8 count = good ? in.read<u8>() : 0;
    for(u8 i = 0; good && i < count && session.views.size() < MAX_VIEWS; i++) {
        View &view = session.views.emplace_back();
        good = readLocation(in, view.location);
        view.time = in.read<i32>();
        if(const f32 zoom = in.read<u8>() != 0 ? in.read<f32>() : 0.f; std::isfinite(zoom) && zoom > 0.f) {
            view.zoom = std::clamp(zoom, 0.1f, 8.f);
        }
        if(const u8 divisor = in.read<u8>() != 0 ? in.read<u8>() : 0; divisor > 0) {
            view.divisor = BeatSnap::clampDivisor(divisor);
        }
        if(in.read<u8>() != 0) view.spacing = Snapping::clampSpacing(in.read<f32>());
        if(in.read<u8>() != 0) view.gridSize = Snapping::clampGridSize(in.read<u8>());
        good = good && in.good();
    }
    if(good) {
        session.tool = in.read<u8>();
        const u32 count = in.read<u32>();
        for(u32 i = 0; good && i < count; i++) {
            session.selection.push_back(in.read<u32>());
            good = in.good();
        }
    }
    if(good && version >= 6) session.tab = in.read<u8>();
    if(!good || !in.good()) return {};

    if(session.open) {
        const Database::MapLocation open = std::move(*session.open);
        session.opened(open);
    }
    return session;
}

std::vector<u8> EditorSession::write() const {
    Packet out;
    out.write<u8>(SESSION_VERSION);
    out.write<u8>(this->open.has_value());
    if(this->open) writeLocation(out, *this->open);
    out.write<u8>(this->open && this->draft);
    if(this->open && this->draft) writeDraft(out, *this->draft);
    out.write<u8>((u8)this->views.size());
    for(const View &view : this->views) {
        writeLocation(out, view.location);
        out.write<i32>(view.time);
        out.write<u8>(view.zoom.has_value());
        if(view.zoom) out.write<f32>(*view.zoom);
        out.write<u8>(view.divisor.has_value());
        if(view.divisor) out.write<u8>((u8)*view.divisor);
        out.write<u8>(view.spacing.has_value());
        if(view.spacing) out.write<f32>(*view.spacing);
        out.write<u8>(view.gridSize.has_value());
        if(view.gridSize) out.write<u8>((u8)*view.gridSize);
    }
    out.write<u8>(this->tool.value_or(0));
    out.write<u32>((u32)this->selection.size());
    for(const u32 number : this->selection) out.write<u32>(number);
    out.write<u8>(this->tab);
    return std::move(out.data);
}

void EditorSession::opened(const Database::MapLocation &location) {
    this->open = location;
    View view;
    // a map in the songs folder also has the view its copy has, see Database::standIn
    if(auto it = Mc::ranges::find(this->views, Database::standIn(location),
                                  [](const View &v) { return Database::standIn(v.location); });
       it != this->views.end()) {
        view = std::move(*it);
        this->views.erase(it);
    }
    view.location = location;
    this->views.insert(this->views.begin(), std::move(view));
    if(this->views.size() > MAX_VIEWS) this->views.resize(MAX_VIEWS);
}

void EditorSession::moved(const Database::MapLocation &location) {
    if(!this->open) return;
    std::erase_if(this->views, [&](const View &view) { return view.location == location; });
    if(auto it = Mc::ranges::find(this->views, *this->open, &View::location); it != this->views.end()) {
        it->location = location;
    }
    this->open = location;
    dropDraft(location);
}

bool EditorSession::putDraft(const Database::MapLocation &location, const Draft &draft) {
    ByteBufferedFile::Writer file(draftFile(location));
    file.write<u8>(DRAFT_VERSION);
    writeLocation(file, location);
    writeDraft(file, draft);
    if(file.commit()) return true;
    debugLog("couldn't keep the draft of {}: {}", location.file, file.error());
    return false;
}

std::optional<EditorSession::Draft> EditorSession::draftOf(const Database::MapLocation &location) {
    std::vector<u8> bytes;
    if(File file(draftFile(location)); file.canRead()) file.readToVector(bytes);
    if(bytes.empty()) return std::nullopt;

    PacketReader in(bytes);
    Database::MapLocation of;
    Draft draft;
    // the file is named by a hash for its location, which it says too
    if(in.read<u8>() != DRAFT_VERSION || !readLocation(in, of) ||
       Database::standIn(of) != Database::standIn(location) || !readDraft(in, draft)) {
        return std::nullopt;
    }
    return draft;
}

void EditorSession::dropDraft(const Database::MapLocation &location) { Environment::deleteFile(draftFile(location)); }
