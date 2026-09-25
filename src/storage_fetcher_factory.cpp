#include "storage_fetcher_factory.h"
#include "storage_fetcher.h"

#include "logos_sdk.h"

#include <cstdint>
#include <cstdio>
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
            return modules.storage_module.isRunning();
        };

    StorageFetcher::Network network =
        [&modules]() {
            return makeNetwork(modules);
        };

    // Use a shared_ptr to keep the fetcher alive because there is currently
    // no way to unsubscribe events.
    return std::shared_ptr<lgpd::Fetcher>(
        new StorageFetcher(downloadToUrl, onStorageDownloadDone,
                           onStorageDownloadProgress, downloadCancel,
                           downloadManifest, onStorageDownloadManifestDone,
                           nodeRunning, network),
        [](lgpd::Fetcher*) {});
}

std::string makeNetwork(LogosModules& modules) {
    if (!modules.modules_state.is_ready("storage_module")) {
        return std::string();
    }

    const StdLogosResult r = modules.storage_module.network();

    if (!r.success || !r.value.is_string()) {
        return std::string();
    }

    return r.value.get<std::string>();
}

// The factory is swapped for a mock in unit tests, which have no logos_sdk.h,
// so we keep watchStorageReady here and don't rely on modules.modules_state in the mock.
std::function<void()> watchStorageReady(LogosModules& modules, std::function<void()> onReady) {
    const logos::SubHandle state = modules.modules_state.onModule_state_changed(
        [onReady](const std::string& module,
                  const LogosMap&, const LogosMap&,
                  const std::string&,
                  const std::string& newState,
                  const LogosMap&, std::uint64_t) {
            if (module == "storage_module" && newState == "ready") {
                onReady();
            }
        });

    // storage_module can be ready before this subscription arms: no ready edge
    // arrives then, so the Armed replay checks is_ready once.
    modules.modules_state.onSubscriptionStatus(
        [&modules, onReady](logos::SubStatus status, std::uint64_t) {
            if (status == logos::SubStatus::Armed &&
                modules.modules_state.is_ready("storage_module")) {
                onReady();
            }
        });

    return [&modules, state]() {
        state.cancel();
        modules.modules_state.onSubscriptionStatus({});
    };
}

StorageNode makeStorageNode(LogosModules& modules) {
    StorageNode node;

    node.isRunning = [&modules]() {
        return modules.storage_module.isRunning();
    };

    node.loadConfig = [&modules](std::string& error) {
        const StdLogosResult r = modules.storage_module.loadConfigOrDefault();

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
