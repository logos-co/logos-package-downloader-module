#include "storage_fetcher_factory.h"
#include "storage_fetcher.h"

#include "logos_sdk.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

constexpr int64_t chunkSize = 65536;

// The node configuration, shared with the Storage UI.
std::string sharedConfig(std::string& error) {
    const char* home = std::getenv("HOME");

    if (!home || !*home) {
        return {};
    }

    const fs::path path = fs::path(home) / ".logos_storage" / "config.json";

    std::error_code ec;

    if (!fs::exists(path, ec)) {
        return {};
    }

    std::ifstream file(path);

    if (!file) {
        error = "cannot read " + path.string();
        return {};
    }

    std::ostringstream config;

    config << file.rdbuf();

    return config.str();
}

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
            return modules.storage_module.isRunning();
        };

    // Use a shared_ptr to keep the fetcher alive because there is currently
    // no way to unsubscribe events.
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
// so we keep watchStorageReady here and don't rely on modules.modules_state in the mock.
std::function<void()> watchStorageReady(LogosModules& modules, std::function<void(bool)> onChange) {
    const logos::SubHandle subscription = modules.modules_state.onModule_state_changed(
        [onChange](const std::string& module,
                   const LogosMap&, const LogosMap&,
                   const std::string&,
                   const std::string& newState,
                   const LogosMap&, std::uint64_t) {
            if (module == "storage_module") {
                onChange(newState == "ready");
            }
        });

    if (modules.modules_state.is_ready("storage_module")) {
        onChange(true);
    }

    return [subscription]() {
        subscription.cancel();
    };
}

StorageNode makeStorageNode(LogosModules& modules) {
    StorageNode node;

    node.migrateConfig = [&modules](std::string& error) {
        const std::string config = sharedConfig(error);

        if (!error.empty()) {
            return std::string();
        }

        const StdLogosResult r = modules.storage_module.migrateConfig(config);

        if (!r.success) {
            error = r.error;
            return std::string();
        }

        if (!r.value.is_string()) {
            error = "the storage module did not return a configuration";
            return std::string();
        }

        return r.value.get<std::string>();
    };

    node.init = [&modules](const std::string& config) {
        return modules.storage_module.init(config);
    };

    node.start = [&modules]() {
        return modules.storage_module.start();
    };

    return node;
}
