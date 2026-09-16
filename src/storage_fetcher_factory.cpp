#include "storage_fetcher_factory.h"
#include "storage_fetcher.h"

#include "logos_sdk.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace {

constexpr int64_t chunkSize = 65536;

}

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules& modules) {
    StorageFetcher::DownloadToUrl downloadToUrl =
        [&modules](const std::string& cid, const std::string& path) {
            const StdLogosResult r =
                modules.storage_module.downloadToUrl(cid, path, false, chunkSize);

            return r.success ? std::string() : r.error;
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&modules](std::function<void(const std::string&)> callback) {
            return modules.storage_module.onStorageDownloadDone(std::move(callback));
        };

    StorageFetcher::OnStorageDownloadProgress onStorageDownloadProgress =
        [&modules](std::function<void(const std::string&)> callback) {
            return modules.storage_module.onStorageDownloadProgress(std::move(callback));
        };

    StorageFetcher::DownloadCancel downloadCancel =
        [&modules](const std::string& cid) {
            const StdLogosResult r = modules.storage_module.downloadCancel(cid);

            return r.success ? std::string() : r.error;
        };

    StorageFetcher::DownloadManifest downloadManifest =
        [&modules](const std::string& cid) {
            const StdLogosResult r = modules.storage_module.downloadManifest(cid);

            return r.success ? std::string() : r.error;
        };

    StorageFetcher::OnStorageDownloadManifestDone onStorageDownloadManifestDone =
        [&modules](std::function<void(const std::string&)> callback) {
            return modules.storage_module.onStorageDownloadManifestDone(std::move(callback));
        };

    StorageFetcher::NodeRunning nodeRunning =
        [&modules]() {
            const StdLogosResult r = modules.storage_module.state();

            return r.success && r.value.is_string() && r.value.get<std::string>() == "running";
        };

    // Stays alive for the whole process: the event callbacks still point at it.
    return std::shared_ptr<lgpd::Fetcher>(
        new StorageFetcher(downloadToUrl, onStorageDownloadDone,
                           onStorageDownloadProgress, downloadCancel,
                           downloadManifest, onStorageDownloadManifestDone,
                           nodeRunning),
        [](lgpd::Fetcher*) {});
}

std::string makeNetwork(LogosModules& modules) {
    const StdLogosResult r = modules.storage_module.network();

    if (!r.success || !r.value.is_string()) {
        return std::string();
    }

    return r.value.get<std::string>();
}

// The factory is swapped for a mock in unit tests, which have no logos_sdk.h,
// so we keep watchStorageReady here and don't rely on modules.modules_state.
void watchStorageReady(LogosModules& modules, std::function<void(bool)> onChange) {
    modules.modules_state.onModule_state_changed(
        [onChange](const std::string& module,
                   const LogosMap&, const LogosMap&,
                   const std::string&,
                   const std::string& newState,
                   const LogosMap&, std::uint64_t) {
            if (module == "storage_module") {
                onChange(newState == "ready");
            }
        });

    onChange(modules.modules_state.is_ready("storage_module"));
}
