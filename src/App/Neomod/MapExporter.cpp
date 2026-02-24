// Copyright (c) 2026, WH, All rights reserved.
#include "MapExporter.h"

#include "OsuConfig.h"
#include "OsuConVars.h"
#include "Timing.h"
#include "Archival.h"
#include "File.h"
#include "Environment.h"
#include "Logging.h"
#include "AsyncPool.h"
#include "fmt/chrono.h"

#include <ctime>

namespace MapExporter {

Async::CancellableHandle<void> submit_export(
    std::set<ExportContext> contexts,
    Async::Channel<Notification> &out) {
    return Async::submit_cancellable([contexts = std::move(contexts), &out](const Sync::stop_token &tok) mutable -> void {
        const std::string export_folder_top = []() -> std::string {
            std::string temp = cv::export_folder.getString();
            if(temp.empty()) {
                temp = NEOMOD_DATA_DIR "exports/"sv;
            }
            File::normalizeSlashes(temp, '\\', '/');
            if(!temp.ends_with('/')) {
                temp.push_back('/');
            }
            return temp;
        }();

        if(!Environment::directoryExists(export_folder_top)) {
            if(!Environment::createDirectory(export_folder_top)) {
                debugLog("Could not create folder {} for exporting into.");
                return;
            }
        }

        const size_t queue_runs = contexts.size();

        for(auto &[beatmap_folder_paths, toplevel_archive, progress_callback] : contexts) {
            const bool single_archive = !toplevel_archive.empty();

            const auto finish = [&]() -> void {
                progress_callback(1.f, "");
                return;
            };

            if(tok.stop_requested()) return finish();

            std::string export_folder_sub =
                single_archive
                    ? export_folder_top + fmt::format("temp-{:%F-%H-%M-%S}/", fmt::gmtime(std::time(nullptr)))
                    : export_folder_top;

            if(single_archive) {
                if(!Environment::createDirectory(export_folder_sub)) {
                    // spill into root? this should be impossible
                    export_folder_sub = export_folder_top;
                }
            }

            std::vector<std::string> real_mapfolders;
            // make sure everything requested actually exists on disk
            for(const auto &temppath : beatmap_folder_paths) {
                if(tok.stop_requested()) return finish();
                std::string current = temppath;
                using enum File::FILETYPE;
                if(auto type = File::existsCaseInsensitive(current); type != FOLDER) {
                    debugLog("requested folder {} {} for export, skipping.", current,
                             type == FILE ? "is a file" : "does not exist");
                } else {
                    // cleanup
                    File::normalizeSlashes(current, '\\', '/');
                    if(current.back() == '/') {
                        current.pop_back();
                    }
                    if(current.empty()) {
                        debugLog("got final folder {} from {} after normalizing, skipping", current, temppath);
                    } else {
                        real_mapfolders.emplace_back(current);
                    }
                }
            }

            size_t total_chunk = real_mapfolders.size();
            size_t current_processing = 0;
            float progress_chunk = 0.f;

            const auto update_progress_stage1 = [&](std::string_view processing) -> void {
                ++current_processing;
                progress_chunk = (float)current_processing / (float)total_chunk;

                // HACKHACK: need actual compression progress for this to be proper,
                // the large zip file creation takes a ton of time
                if(single_archive) {
                    progress_chunk /= 2.f;
                }
                float progress = std::clamp(progress_chunk / (float)queue_runs, 0.01f, 0.99f);
                progress_callback(progress, Environment::getFileNameFromFilePath(processing));
            };

            auto queue_notification = [&](std::string_view message, bool success, std::string callback_open_path = "") {
                if(!callback_open_path.empty()) {
                    out.push({UString(message), success,
                              [path = std::move(callback_open_path)]() -> void { return env->openFileBrowser(std::move(path)); }});
                } else {
                    out.push({UString(message), success, {}});
                }
            };

            std::vector<std::string> exported;
            for(const auto &folder : real_mapfolders) {
                if(tok.stop_requested()) return finish();
                Archive::Writer ar;
                if(!ar.addPath(folder, "", tok)) {
                    update_progress_stage1(folder);
                    continue;
                }

                std::string cleaned_folder_name = folder;
                const size_t slashpos = folder.rfind('/');
                if(slashpos != std::string::npos) {
                    cleaned_folder_name = folder.substr(slashpos + 1);
                }

                const std::string path_to_export_to =
                    fmt::format("{}{}.osz", export_folder_sub, cleaned_folder_name);

                // even if we're bundling everything at the end, write out to a file for each entry
                // to avoid needing to store each individual entry in memory at once
                if(ar.writeToFile(path_to_export_to, false, tok)) {
                    exported.push_back(path_to_export_to);
                }
                update_progress_stage1(folder);
            }

            if(!single_archive) {
                // limit spam
                if(exported.size() < 10) {
                    if(exported.empty()) {
                        queue_notification(fmt::format("Failed to export folders to {}", export_folder_sub),
                                                 false);
                    } else {
                        for(auto &exported_entry : exported) {
                            queue_notification(fmt::format("Exported {}", exported_entry), true,
                                                     exported_entry);
                        }
                    }
                } else {
                    queue_notification(
                        fmt::format("Exported {} folders to {}", exported.size(), export_folder_sub), true,
                        export_folder_sub);
                }
            }
            if(single_archive && !exported.empty()) {
                progress_chunk = 0.50f;
                current_processing = 0;
                total_chunk = exported.size();

                const auto update_progress_stage2 = [&](std::string_view processing) -> void {
                    ++current_processing;
                    // leave 25% for final archive creation
                    progress_chunk = 0.50f + ((float)current_processing / (float)total_chunk) * 0.25f;
                    float progress = std::clamp(progress_chunk / (float)queue_runs, 0.01f, 0.99f);
                    progress_callback(progress, Environment::getFileNameFromFilePath(processing));
                };

                // re-compressing things we already compressed is a waste of resources
                Archive::Writer ar(Archive::Format::ZIP, Archive::COMPRESSION_STORE);
                const std::string extSuffix{ar.getExtSuffix()};

                size_t num_added = 0;
                for(const auto &exported_entry : exported) {
                    if(tok.stop_requested()) return finish();
                    num_added += ar.addPath(exported_entry, "", tok);
                    update_progress_stage2(exported_entry);
                }
                if(num_added) {
                    if(tok.stop_requested()) return finish();
                    // not subfolder, toplevel
                    const std::string export_pathname =
                        fmt::format("{}{}", export_folder_top, toplevel_archive, fmt::gmtime(std::time(nullptr)));

                    update_progress_stage2(export_pathname + extSuffix);

                    if(ar.writeToFile(export_pathname, true, tok)) {
                        queue_notification(
                            fmt::format("Exported {} folders into {}{}", num_added, export_pathname, extSuffix),
                            true, export_pathname);
                    } else {
                        queue_notification(fmt::format("Failed to export {} folders into {}{}", num_added,
                                                             export_pathname, extSuffix),
                                                 false);
                    }
                } else {
                    queue_notification(
                        fmt::format("Failed to export folders to {}{}", export_folder_top, extSuffix), false);
                }
                // clean up temp dir
                // this is sketchy so i'll only delete if the export folder hasn't been changed,
                // for now (TEMP)
                if(cv::export_folder.isDefault()) {
                    Environment::deletePathsRecursive(export_folder_sub);
                }
            } else if(single_archive) {
                queue_notification(fmt::format("Failed to export folders to {}{}", export_folder_sub,
                                                     Archive::getExtSuffix(Archive::Format::ZIP)),
                                         false);
            }

            float progress = std::clamp(1.f / (float)queue_runs, 0.01f, 1.f);
            progress_callback(progress, "");
        }
    }, Lane::Background);
}

}  // namespace MapExporter
