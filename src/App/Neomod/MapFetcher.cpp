// Copyright (c) 2026, WH, All rights reserved.

#include "MapFetcher.h"

#include "BeatmapInstaller.h"
#include "Database.h"
#include "Downloader.h"
#include "Osu.h"

void MapFetcher::target_map(i32 map_id, const MD5Hash &md5, i32 set_id_hint) {
    if(map_id <= 0) {
        this->clear();
        return;
    }
    // same target (including an unchanged success criterion): keep all progress/verdict state
    if(this->map_id == map_id && this->md5 == md5) return;

    this->clear();
    this->map_id = map_id;
    this->md5 = md5;
    this->set_id_hint = set_id_hint;
    this->st.status = Status::Working;
}

void MapFetcher::target_set(i32 set_id) {
    if(set_id <= 0) {
        this->clear();
        return;
    }
    if(this->map_id == 0 && this->set_id == set_id) return;

    this->clear();
    this->set_id = set_id;
    this->st.status = Status::Working;
}

void MapFetcher::clear() {
    this->md5.clear();
    this->map_id = 0;
    this->set_id = 0;
    this->set_id_hint = 0;
    this->found = nullptr;
    this->in_flight = false;
    this->st = {};
}

DatabaseBeatmap *MapFetcher::lookup() const {
    if(this->map_id != 0) {
        return this->md5.empty() ? db->getBeatmapDifficulty(this->map_id) : db->getBeatmapDifficulty(this->md5);
    }
    return this->set_id > 0 ? db->getBeatmapSet(this->set_id) : nullptr;
}

const MapFetcher::State &MapFetcher::tick() {
    if(this->st.status == Status::Idle) return this->st;

    // a satisfied target always wins, even after giving up (e.g. the user imports the map some
    // other way while the failure text is showing)
    if((this->found = this->lookup()) != nullptr) {
        this->st = {.status = Status::Found, .why = Why::None, .progress = 1.f};
        return this->st;
    }
    if(this->st.status == Status::NotFound) return this->st;  // gave up; only the db check above revives us
    this->st = {.status = Status::Working, .why = Why::None, .progress = 0.f};

    // map targets need the owning set resolved first (server metadata query unless hinted)
    if(this->set_id <= 0) {
        const i32 resolved = Downloader::resolve_beatmapset_id_for(this->map_id, this->set_id_hint);
        if(resolved < 0) {
            this->st = {.status = Status::NotFound, .why = Why::Resolve, .progress = 0.f};
        } else if(resolved > 0) {
            this->set_id = resolved;
        }
        // 0: query still in flight, stay Working
        return this->st;
    }

    const auto inst = osu->getBeatmapInstaller()->get_state(this->set_id);
    using enum MapInstallStage;
    if(inst.stage == Failed) {
        this->st = {.status = Status::NotFound, .why = Why::Install, .progress = 0.f};
        return this->st;
    }
    if(inst.stage != None) {
        this->in_flight = true;
        this->st.progress = inst.progress;
        return this->st;
    }

    // nothing tracked for this set right now. if we previously watched an entry, that install
    // completed (Done entries are erased immediately) without our target appearing in the db: the
    // server's version of the set doesn't contain the target, and downloading the same bytes again
    // won't change that, so give up.
    if(this->in_flight) {
        this->in_flight = false;
        this->st = {.status = Status::NotFound, .why = Why::Mismatch, .progress = 0.f};
        return this->st;
    }

    osu->getBeatmapInstaller()->enqueue(this->set_id, /*auto_select=*/false);
    return this->st;
}
