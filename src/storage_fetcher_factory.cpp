#include "storage_fetcher_factory.h"
#include "storage_fetcher.h"

#include "logos_sdk.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
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

    if (!fs::exists(path)) {
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
            const StdLogosResult r = modules.storage_module.state();

            return r.success && r.value.is_string() && r.value.get<std::string>() == "running";
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

StorageNode makeStorageNode(LogosModules& modules) {
    StorageNode node;

    node.state = [&modules]() {
        const StdLogosResult r = modules.storage_module.state();

        return r.success && r.value.is_string() ? r.value.get<std::string>() : std::string();
    };

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

    node.stop = [&modules]() {
        return modules.storage_module.stop().success;
    };

    node.destroy = [&modules]() {
        modules.storage_module.destroy();
    };

    // Used to cancel the subscription to the end of the stop.
    auto stopSubscription = std::make_shared<logos::SubHandle>();

    node.onStopped = [&modules, stopSubscription](std::function<void(bool)> callback) {
        *stopSubscription = modules.storage_module.onStorageStop([callback](const std::string& payload) {
            bool stopped = false;

            try {
                stopped = LogosMap::parse(payload).value("success", false);
            } catch (...) {
                stopped = false;
            }

            callback(stopped);
        });

        return static_cast<bool>(*stopSubscription);
    };

    node.cancelSubscription = [stopSubscription]() {
        stopSubscription->cancel();
    };

    return node;
}
