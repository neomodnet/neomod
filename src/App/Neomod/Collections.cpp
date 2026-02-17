// Copyright (c) 2024, kiwec, All rights reserved.
#include "Collections.h"

#include "ByteBufferedFile.h"
#include "OsuConVars.h"
#include "Database.h"
#include "Engine.h"
#include "Timing.h"
#include "Logging.h"

#include <algorithm>
#include <utility>

namespace Collections {

namespace {  // static namespace
bool s_collections_loaded{false};
std::vector<Collection> s_collections;

}  // namespace

std::vector<Collection>& get_loaded() { return s_collections; }

bool delete_collection(std::string_view collection_name) {
    if(collection_name.empty() || s_collections.empty()) return false;

    const size_t erased = std::erase_if(
        s_collections, [collection_name](const auto& col) -> bool { return col.name == collection_name; });
    return erased > 0;
}

void Collection::add_map(const MD5Hash& map_hash) {
    // remove from deleted maps
    this->deleted_maps.erase(map_hash);

    // add to neomod maps
    this->neomod_maps.insert(map_hash);

    // add to maps (TODO: what's the difference...?)
    this->maps.insert(map_hash);
}

void Collection::remove_map(const MD5Hash& map_hash) {
    // remove from maps (@spec: i just havent tried to understand the logic but im sure theres a reason for this vs neomod_maps)
    this->maps.erase(map_hash);

    // remove from neomod maps
    this->neomod_maps.erase(map_hash);

    // add to deleted maps
    if(this->peppy_maps.contains(map_hash)) {
        this->deleted_maps.insert(map_hash);
    }
}

bool Collection::rename_to(std::string_view new_name) {
    if(new_name.empty() || new_name == this->name) return false;

    // don't allow renaming to an existing collection name
    if(std::ranges::contains(s_collections, new_name, [](const auto& col) -> std::string_view { return col.name; })) {
        debugLog("not renaming {} -> {}, conflicting name", this->name, new_name);
        return false;
    }

    this->name = new_name;

    return true;
}

Collection& get_or_create_collection(std::string_view name) {
    if(name.length() < 1) name = "Untitled collection";

    // get
    const auto& it = std::ranges::find_if(s_collections, [name](const auto& col) -> bool { return col.name == name; });
    if(it != s_collections.end()) {
        return *it;
    }

    // create
    Collection collection{};
    collection.name = name;

    auto& ret = s_collections.emplace_back(std::move(collection));

    return ret;
}

// Should only be called from db loader thread!
bool load_peppy(std::string_view peppy_collections_path) {
    ByteBufferedFile::Reader peppy_collections(peppy_collections_path);
    if(peppy_collections.total_size == 0) return false;
    if(!cv::collections_legacy_enabled.getBool()) {
        db->bytes_processed += peppy_collections.total_size;
        return false;
    }

    u32 version = peppy_collections.read<u32>();
    if(version > cv::database_version.getVal<u32>() && !cv::database_ignore_version.getBool()) {
        debugLog("osu!stable collection.db (version {}) is newer than latest supported (version {})!", version,
                 cv::database_version.getVal<u32>());
        db->bytes_processed += peppy_collections.total_size;
        return false;
    }

    u32 total_maps = 0;
    u32 nb_collections = peppy_collections.read<u32>();
    for(u32 c = 0; c < nb_collections; c++) {
        auto name = peppy_collections.read_string();
        u32 nb_maps = peppy_collections.read<u32>();
        total_maps += nb_maps;

        auto& collection = get_or_create_collection(name);
        collection.maps.reserve(nb_maps);
        collection.peppy_maps.reserve(nb_maps);

        for(u32 m = 0; m < nb_maps; m++) {
            MD5String map_hash;
            (void)peppy_collections.read_hash_chars(map_hash);  // TODO: validate

            collection.maps.insert(map_hash);
            collection.peppy_maps.insert(map_hash);
        }

        u32 progress_bytes = db->bytes_processed + peppy_collections.total_pos;
        f64 progress_float = (f64)progress_bytes / (f64)db->total_bytes;
        db->loading_progress = std::clamp(progress_float, 0.01, 0.99);
    }

    debugLog("Loaded {:d} peppy collections ({:d} maps)", nb_collections, total_maps);
    db->bytes_processed += peppy_collections.total_size;
    return true;
}

// Should only be called from db loader thread!
bool load_mcneomod(std::string_view neomod_collections_path) {
    ByteBufferedFile::Reader neomod_collections(neomod_collections_path);
    if(neomod_collections.total_size == 0) return false;

    u32 total_maps = 0;

    u32 version = neomod_collections.read<u32>();
    u32 nb_collections = neomod_collections.read<u32>();

    if(version > COLLECTIONS_DB_VERSION) {
        debugLog("neomod collections.db version is too recent! Cannot load it without stuff breaking.");
        db->bytes_processed += neomod_collections.total_size;
        return false;
    }

    for(u32 c = 0; c < nb_collections; c++) {
        auto name = neomod_collections.read_string();
        auto& collection = get_or_create_collection(name);

        u32 nb_deleted_maps = 0;
        if(version >= 20240429) {
            nb_deleted_maps = neomod_collections.read<u32>();
        }

        collection.deleted_maps.reserve(nb_deleted_maps);
        for(u32 d = 0; d < nb_deleted_maps; d++) {
            MD5String map_hash;
            (void)neomod_collections.read_hash_chars(map_hash);  // TODO: validate

            collection.maps.erase(map_hash);
            collection.deleted_maps.insert(map_hash);
        }

        u32 nb_maps = neomod_collections.read<u32>();
        total_maps += nb_maps;
        collection.maps.reserve(collection.maps.size() + nb_maps);
        collection.neomod_maps.reserve(nb_maps);

        for(u32 m = 0; m < nb_maps; m++) {
            MD5String map_hash;
            (void)neomod_collections.read_hash_chars(map_hash);  // TODO: validate

            collection.maps.insert(map_hash);
            collection.neomod_maps.insert(map_hash);
        }

        u32 progress_bytes = db->bytes_processed + neomod_collections.total_pos;
        f64 progress_float = (f64)progress_bytes / (f64)db->total_bytes;
        db->loading_progress = std::clamp(progress_float, 0.01, 0.99);
    }

    debugLog("Loaded {:d} neomod collections ({:d} maps)", nb_collections, total_maps);
    db->bytes_processed += neomod_collections.total_size;
    return true;
}

// Should only be called from db loader thread!
bool load_all() {
    const double startTime = Timing::getTimeReal();

    unload_all();

    const auto& peppy_path = db->database_files[Database::DatabaseType::STABLE_COLLECTIONS];
    load_peppy(peppy_path);

    const auto& mcneomod_path = db->database_files[Database::DatabaseType::MCNEOMOD_COLLECTIONS];
    load_mcneomod(mcneomod_path);

    debugLog("peppy+neomod collections: loading took {:f} seconds", (Timing::getTimeReal() - startTime));
    s_collections_loaded = true;
    return true;
}

void unload_all() {
    s_collections_loaded = false;

    s_collections.clear();
}

bool save_collections() {
    debugLog("Osu: Saving collections ...");
    if(!s_collections_loaded) {
        debugLog("Cannot save collections since they weren't loaded properly first!");
        return false;
    }

    const double startTime = Timing::getTimeReal();

    const auto neomod_collections_db = Database::getDBPath(Database::DatabaseType::MCNEOMOD_COLLECTIONS);

    ByteBufferedFile::Writer db(neomod_collections_db);
    if(!db.good()) {
        debugLog("Cannot save collections to {}: {}", neomod_collections_db, db.error());
        return false;
    }

    db.write<u32>(COLLECTIONS_DB_VERSION);

    u32 nb_collections = s_collections.size();
    db.write<u32>(nb_collections);

    for(const auto& collection : s_collections) {
        db.write_string(collection.name);

        u32 nb_deleted = collection.deleted_maps.size();
        db.write<u32>(nb_deleted);
        for(const auto& mapmd5 : collection.deleted_maps) {
            db.write_hash_chars(mapmd5);
        }

        u32 nb_neomod = collection.neomod_maps.size();
        db.write<u32>(nb_neomod);
        for(const auto& mapmd5 : collection.neomod_maps) {
            db.write_hash_chars(mapmd5);
        }
    }

    debugLog("collections.db: saving took {:f} seconds", (Timing::getTimeReal() - startTime));
    return true;
}
}  // namespace Collections
