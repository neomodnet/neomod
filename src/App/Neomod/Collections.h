#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "MD5Hash.h"
#include "Hashing.h"

#include <vector>

#define COLLECTIONS_DB_VERSION 20240429
namespace Collections {

class Collection;

std::vector<Collection> &get_loaded();

[[nodiscard]] bool delete_collection(std::string_view collection_name);

Collection &get_or_create_collection(std::string_view name);

bool load_all(std::string_view neomod_collections_path, std::string_view peppy_collections_path);
bool load_peppy(std::string_view peppy_collections_path);
bool load_mcneomod(std::string_view neomod_collections_path);
void unload_all();
bool save_collections();

class Collection {
    std::string name;
    Hash::flat::set<MD5Hash> maps;

    Hash::flat::set<MD5Hash> neomod_maps;
    Hash::flat::set<MD5Hash> peppy_maps;
    Hash::flat::set<MD5Hash> deleted_maps;

    friend bool delete_collection(std::string_view collection_name);
    friend Collection &get_or_create_collection(std::string_view name);

    friend bool load_all(std::string_view neomod_collections_path, std::string_view peppy_collections_path);
    friend bool load_peppy(std::string_view peppy_collections_path);
    friend bool load_mcneomod(std::string_view neomod_collections_path);
    friend void unload_all();
    friend bool save_collections();

   public:
    [[nodiscard]] inline const std::string &get_name() const { return this->name; }
    [[nodiscard]] inline const Hash::flat::set<MD5Hash> &get_maps() const { return this->maps; }

    void add_map(const MD5Hash &map_hash);
    void remove_map(const MD5Hash &map_hash);
    [[nodiscard]] bool rename_to(std::string_view new_name);
};

}  // namespace Collections
