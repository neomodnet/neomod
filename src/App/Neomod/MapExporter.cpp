// Copyright (c) 2026, WH, All rights reserved.
#include "MapExporter.h"

#include "NotificationOverlay.h"
#include "OsuConVars.h"
#include "Osu.h"
#include "Timing.h"
#include "UI.h"
#include "Archival.h"
#include "File.h"
#include "ResourceManager.h"
#include "Resource.h"
#include "Engine.h"
#include "Environment.h"
#include "Logging.h"
#include "fmt/chrono.h"
#include "UString.h"
#include "SyncStoptoken.h"

#include <ctime>
#include <set>
#include <atomic>

namespace MapExporter {

namespace {

struct Exporter final {
    // we don't deallocate it ourselves, ResourceManager will
    Exporter() : worker(new Dispatchee(this)) {}
    void addToQueue(ExportContext &&ctx) {
        if(auto [_, added_unique] = this->export_queue.emplace(std::move(ctx)); added_unique) {
            this->worker->go_reload_urself();
        }
    }

   private:
    class Dispatchee : public Resource {
        NOCOPY_NOMOVE(Dispatchee)
       public:
        Dispatchee(Exporter *parent) : Resource(APPDEFINED), parent(parent) {
            resourceManager->addManagedResource(this, "CompressionExportWorker");
            this->setReady(true);
            this->setAsyncReady(true);
        }
        ~Dispatchee() override {
            this->destroy();
            parent->worker = nullptr;
        }

        void go_reload_urself() {
            if(this->isReady()) {
                this->export_queue_real = std::move(parent->export_queue);
                resourceManager->reloadResource(this, true);
            } else {
                this->reload_pending = true;
            }
        }

        void init() override {
            // we *could* (maybe) be called here if osu has shut down/is shutting down
            // so check this explicitly and abort everything if so
            // (probably only in -testapp mode if shutting down osu but not engine)
            if(!(ui && ui->getNotificationOverlay())) {
                this->reload_pending = false;
                this->export_queue_real.clear();
                this->notification_queue.clear();
                this->setReady(true);
                this->setAsyncReady(true);
                return;
            }
            this->export_queue_real.clear();
            for(const auto &[msg, succeeded, cb] : this->notification_queue) {
                ui->getNotificationOverlay()->addToast(msg, succeeded ? SUCCESS_TOAST : ERROR_TOAST, cb);
            }
            this->notification_queue.clear();

            if(!this->reload_pending) {
                this->setReady(true);
                this->setAsyncReady(true);
            } else {
                this->reload_pending = false;
                this->export_queue_real = std::move(parent->export_queue);
                resourceManager->reloadResource(this, true);
            }
        }

        void initAsync() override {
            this->stop_source = {};
            this->setReady(false);
            this->setAsyncReady(false);
            this->progress = -1.f;

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
                    this->setReady(true);
                    this->setAsyncReady(true);
                    return;
                }
            }

            this->progress = 0.f;
            const size_t queue_runs = this->export_queue_real.size();

            for(auto &[beatmap_folder_paths, toplevel_archive, progress_callback] : this->export_queue_real) {
                const bool single_archive = !toplevel_archive.empty();

                const auto finish = [&]() -> void {
                    progress_callback(1.f, "");
                    this->setReady(true);
                    this->setAsyncReady(true);
                    return;
                };

                if(this->isInterrupted()) return finish();

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
                    if(this->isInterrupted()) return finish();
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
                    this->progress = std::clamp(progress_chunk / (float)queue_runs, 0.01f, 0.99f);
                    progress_callback(this->progress, Environment::getFileNameFromFilePath(processing));
                };

                std::vector<std::string> exported;
                for(const auto &folder : real_mapfolders) {
                    if(this->isInterrupted()) return finish();
                    Archive::Writer ar;
                    if(!ar.addPath(folder, "", this->stop_source.get_token())) {
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
                    if(ar.writeToFile(path_to_export_to, false, this->stop_source.get_token())) {
                        exported.push_back(path_to_export_to);
                    }
                    update_progress_stage1(folder);
                }

                if(!single_archive) {
                    // limit spam
                    if(exported.size() < 10) {
                        if(exported.empty()) {
                            this->queue_notification(fmt::format("Failed to export folders to {}", export_folder_sub),
                                                     false);
                        } else {
                            for(auto &exported_entry : exported) {
                                this->queue_notification(fmt::format("Exported {}", exported_entry), true,
                                                         exported_entry);
                            }
                        }
                    } else {
                        this->queue_notification(
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
                        this->progress = std::clamp(progress_chunk / (float)queue_runs, 0.01f, 0.99f);
                        progress_callback(this->progress, Environment::getFileNameFromFilePath(processing));
                    };

                    // re-compressing things we already compressed is a waste of resources
                    Archive::Writer ar(Archive::Format::ZIP, Archive::COMPRESSION_STORE);
                    const std::string extSuffix{ar.getExtSuffix()};

                    size_t num_added = 0;
                    for(const auto &exported_entry : exported) {
                        if(this->isInterrupted()) return finish();
                        num_added += ar.addPath(exported_entry, "", this->stop_source.get_token());
                        update_progress_stage2(exported_entry);
                    }
                    if(num_added) {
                        if(this->isInterrupted()) return finish();
                        // not subfolder, toplevel
                        const std::string export_pathname =
                            fmt::format("{}{}", export_folder_top, toplevel_archive, fmt::gmtime(std::time(nullptr)));

                        update_progress_stage2(export_pathname + extSuffix);

                        if(ar.writeToFile(export_pathname, true, this->stop_source.get_token())) {
                            this->queue_notification(
                                fmt::format("Exported {} folders into {}{}", num_added, export_pathname, extSuffix),
                                true, export_pathname);
                        } else {
                            this->queue_notification(fmt::format("Failed to export {} folders into {}{}", num_added,
                                                                 export_pathname, extSuffix),
                                                     false);
                        }
                    } else {
                        this->queue_notification(
                            fmt::format("Failed to export folders to {}{}", export_folder_top, extSuffix), false);
                    }
                    // clean up temp dir
                    // this is sketchy so i'll only delete if the export folder hasn't been changed,
                    // for now (TEMP)
                    if(cv::export_folder.isDefault()) {
                        Environment::deletePathsRecursive(export_folder_sub);
                    }
                } else if(single_archive) {
                    this->queue_notification(fmt::format("Failed to export folders to {}{}", export_folder_sub,
                                                         Archive::getExtSuffix(Archive::Format::ZIP)),
                                             false);
                }

                this->progress = std::clamp(1.f / (float)queue_runs, 0.01f, 1.f);
                progress_callback(this->progress, "");
            }
            this->progress = 1.f;

            this->setAsyncReady(true);
            this->setReady(true);
        }

        void destroy() override { this->interruptLoad(); }

        void interruptLoad() override {
            Resource::interruptLoad();
            this->stop_source.request_stop();
        }

       private:
        struct QueuedNotification {
            UString msg;
            bool success;
            NotificationOverlay::ToastClickCallback cb;
        };
        void queue_notification(std::string_view message, bool success, std::string callback_open_path = "") {
            if(!callback_open_path.empty()) {
                this->notification_queue.emplace_back(
                    message, success,
                    [path = std::move(callback_open_path)]() -> void { return env->openFileBrowser(std::move(path)); });
            } else {
                this->notification_queue.emplace_back(message, success);
            }
        }

        friend Exporter;
        std::set<ExportContext> export_queue_real;
        std::vector<QueuedNotification> notification_queue;
        Exporter *parent;

        Sync::stop_source stop_source;

        float progress{-1.f};

        // only accessed sync
        bool reload_pending{false};
    };

    Dispatchee *worker;
    // this should basically always only contain 1 element
    std::set<ExportContext> export_queue;
};

Exporter &getExporter() {
    static Exporter exporter;
    return exporter;
}
}  // namespace

void export_paths(ExportContext ctx) { getExporter().addToQueue(std::move(ctx)); }

}  // namespace MapExporter

// namespace cv {
// static ConVar test_write_archive(
//     "test_write_archive", ""sv, CLIENT | NOLOAD | NOSAVE | HIDDEN, [](std::string_view /*path*/) -> void {
//         std::vector<std::string> entries{
//             cv::osu_folder.getString() + "/Songs/999090 Aoi - King of Galaxy",
//             cv::osu_folder.getString() + "/Songs/900030 sweet ARMS - Blade of Hope/",
//             cv::osu_folder.getString() + "/Songs/9901 KOTOKO - La Clef ~Meikyuu no Kagi~/"};
//         MapExporter::export_paths(std::move(entries),
//                                   fmt::format("toplevel-archive{}", engine ? engine->getTime() : 0));
//         test_write_archive.setValue("", false);
//     });
// }  // namespace cv
