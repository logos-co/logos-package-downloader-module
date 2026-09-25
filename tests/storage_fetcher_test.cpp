#include <logos_test.h>
#include <logos_json.h>
#include "storage_fetcher.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::string buildPayload(const std::string& cid, bool success, const std::string& error = "") {
    LogosMap payload{{"success", success}, {"sessionId", cid}};

    if (!error.empty()) {
        payload["error"] = error;
    }

    return payload.dump();
}

std::string buildProgressPayload(const std::string& cid, std::uint64_t bytes, std::uint64_t total) {
    return LogosMap{
        {"success", true},
        {"sessionId", cid},
        {"bytes", bytes},
        {"total", total}}.dump();
}

const StorageFetcher::OnStorageDownloadDone unusedDone =
    [](std::function<void(const std::string&)>) { return StorageFetcher::Unsubscribe([]() {}); };

const StorageFetcher::OnStorageDownloadProgress unusedProgress =
    [](std::function<void(const std::string&)>) { return StorageFetcher::Unsubscribe([]() {}); };

const StorageFetcher::NodeRunning nodeRunning = []() { return true; };

const StorageFetcher::Network network = []() { return std::string("logos.test"); };

// For the transfer and the manifest: an event that stops matching fails the
// test instead of hanging it for minutes.
const std::chrono::milliseconds shortTimeout(50);

const StorageFetcher::DownloadCancel unusedCancel =
    [](const std::string&) {
        return std::string();
    };

struct Manifest {
    std::function<void(const std::string&)> fire;

    StorageFetcher::OnStorageDownloadManifestDone subscribe() {
        return [this](std::function<void(const std::string&)> callback) {
            fire = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };
    }

    StorageFetcher::DownloadManifest fetch() {
        return [this](const std::string& cid) {
            fire(LogosMap{{"success", true}, {"cid", cid}}.dump());
            return std::string();
        };
    }
};

// A transfer that lasts longer than the stall timeout, with progress events
// always closer together than it.
lgpd::FetchResult downloadWithSteadyProgress(const lgpd::ProgressFn& onProgress) {
    std::function<void(const std::string&)> fireDone;
    std::function<void(const std::string&)> fireProgress;
    std::thread storage;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string&) {
            storage = std::thread([&, cid]() {
                for (int i = 0; i < 20; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    fireProgress(buildProgressPayload(cid, 100, 2000));
                }

                fireDone(buildPayload(cid, true));
            });

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    StorageFetcher::OnStorageDownloadProgress onStorageDownloadProgress =
        [&](std::function<void(const std::string&)> callback) {
            fireProgress = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    const std::chrono::milliseconds stallTimeout(200);

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, onStorageDownloadProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, stallTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx", onProgress);

    storage.join();

    return r;
}

} // namespace

LOGOS_TEST(getToFile_succeeds) {
    // fireDone is a callback that will be called when the download is done.
    // It will be set by the onStorageDownloadDone callback.
    std::function<void(const std::string&)> fireDone;
    std::string downloadedCid;
    std::string downloadedPath;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string& path) {
            downloadedCid = cid;
            downloadedPath = path;

            const bool success = true;
            const std::string payload = buildPayload(cid, success);

            fireDone(payload);

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(r.ok);
    LOGOS_ASSERT_EQ(downloadedCid, std::string("cid-1"));
    LOGOS_ASSERT_EQ(downloadedPath, std::string("/tmp/wallet.lgx"));
}

LOGOS_TEST(getToFile_returns_the_error_from_the_download_event) {
    std::function<void(const std::string&)> fireDone;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string&) {
            const bool success = false;
            const std::string payload = buildPayload(cid, success, "cid not found");

            fireDone(payload);

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_EQ(r.error, std::string("cid not found"));
}

LOGOS_TEST(getToFile_returns_the_error_when_the_downloadToUrl_returns_an_error) {
    StorageFetcher::DownloadToUrl downloadToUrl =
        [](const std::string&, const std::string&) {
            return std::string("node not started");
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_EQ(r.error, std::string("node not started"));
}

LOGOS_TEST(getToFile_downloads_nothing_when_the_subscription_failed) {
    bool downloaded = false;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            downloaded = true;
            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [](std::function<void(const std::string&)>) {
            return StorageFetcher::Unsubscribe();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_FALSE(downloaded);
}

LOGOS_TEST(getToFile_refuses_a_cid_already_in_progress) {
    std::function<void(const std::string&)> fireDone;
    StorageFetcher* fetcherPtr = nullptr;
    lgpd::FetchResult secondCall;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string& path) {
            // Make a second call to getToFile while the first one is still in progress
            secondCall = fetcherPtr->getToFile(cid, path);

            const bool success = true;
            const std::string payload = buildPayload(cid, success);

            fireDone(payload);

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();
    fetcherPtr = &fetcher;

    lgpd::FetchResult firstCall = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(firstCall.ok);
    LOGOS_ASSERT_FALSE(secondCall.ok);
    LOGOS_ASSERT_TRUE(secondCall.error.find("already in progress") != std::string::npos);
}

LOGOS_TEST(getToFile_times_out_when_no_event_arrives) {
    StorageFetcher::DownloadToUrl downloadToUrl =
        [](const std::string&, const std::string&) {
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_TRUE(r.error.find("timed out") != std::string::npos);
}

LOGOS_TEST(getToFile_is_cancelled_on_timeout) {
    StorageFetcher::DownloadToUrl downloadToUrl =
        [](const std::string&, const std::string&) {
            return std::string();
        };

    std::string cancelledCid;

    StorageFetcher::DownloadCancel downloadCancel =
        [&](const std::string& cid) {
            cancelledCid = cid;
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, downloadCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_EQ(cancelledCid, std::string("cid-1"));
}

LOGOS_TEST(cancelPendingDownloads_fails_a_download_in_progress) {
    StorageFetcher* fetcherPtr = nullptr;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            fetcherPtr->cancelPendingDownloads();
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();
    fetcherPtr = &fetcher;

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(r.error.find("unloading") != std::string::npos);
}

LOGOS_TEST(cancelPendingDownloads_fails_a_manifest_in_progress) {
    StorageFetcher* fetcherPtr = nullptr;

    StorageFetcher::OnStorageDownloadManifestDone onManifestDone =
        [](std::function<void(const std::string&)>) { return StorageFetcher::Unsubscribe([]() {}); };

    StorageFetcher::DownloadManifest downloadManifest =
        [&](const std::string&) {
            fetcherPtr->cancelPendingDownloads();
            return std::string();
        };

    StorageFetcher fetcher(nullptr, unusedDone, unusedProgress, unusedCancel,
                           downloadManifest, onManifestDone, nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();
    fetcherPtr = &fetcher;

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(r.error.find("unloading") != std::string::npos);
}

LOGOS_TEST(getToFile_downloads_nothing_after_cancelPendingDownloads) {
    bool downloaded = false;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            downloaded = true;
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    fetcher.cancelPendingDownloads();
    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(r.error.find("unloading") != std::string::npos);
    LOGOS_ASSERT_FALSE(downloaded);
}

LOGOS_TEST(destroying_the_fetcher_cancels_its_subscriptions) {
    int cancelled = 0;

    auto subscribe = [&](std::function<void(const std::string&)>) {
        return StorageFetcher::Unsubscribe([&]() { ++cancelled; });
    };

    {
        StorageFetcher fetcher(nullptr, subscribe, subscribe, unusedCancel,
                               nullptr, subscribe, nodeRunning, network, shortTimeout, shortTimeout);
        fetcher.subscribe();
    }

    LOGOS_ASSERT_EQ(cancelled, 3);
}

LOGOS_TEST(getToFile_can_retry_a_cid_after_a_timeout) {
    int downloads = 0;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            ++downloads;
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    fetcher.getToFile("cid-1", "/tmp/wallet.lgx");
    fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_EQ(downloads, 2);
}

LOGOS_TEST(getToFile_times_out_when_the_manifest_never_arrives) {
    bool downloaded = false;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            downloaded = true;
            return std::string();
        };

    StorageFetcher::OnStorageDownloadManifestDone onManifestDone =
        [](std::function<void(const std::string&)>) { return StorageFetcher::Unsubscribe([]() {}); };

    StorageFetcher::DownloadManifest downloadManifest =
        [](const std::string&) { return std::string(); };

    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           downloadManifest, onManifestDone, nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_TRUE(r.error.find("timed out") != std::string::npos);
    LOGOS_ASSERT_FALSE(downloaded);
}

LOGOS_TEST(getToFile_keeps_a_download_that_makes_progress) {
    lgpd::FetchResult r = downloadWithSteadyProgress([](std::uint64_t, std::uint64_t) {});

    LOGOS_ASSERT_TRUE(r.ok);
}

LOGOS_TEST(getToFile_keeps_a_download_that_makes_progress_without_a_progress_callback) {
    lgpd::FetchResult r = downloadWithSteadyProgress(lgpd::ProgressFn{});

    LOGOS_ASSERT_TRUE(r.ok);
}

LOGOS_TEST(getToFile_accumulates_the_progress_data_size) {
    std::function<void(const std::string&)> fireDone;
    std::function<void(const std::string&)> fireProgress;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string&) {
            fireProgress(buildProgressPayload(cid, 100, 4096));
            fireProgress(buildProgressPayload(cid, 200, 4096));

            fireDone(buildPayload(cid, true));

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    StorageFetcher::OnStorageDownloadProgress onStorageDownloadProgress =
        [&](std::function<void(const std::string&)> callback) {
            fireProgress = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, onStorageDownloadProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    std::vector<std::pair<std::uint64_t, std::uint64_t>> samples;

    lgpd::ProgressFn onProgress =
        [&](std::uint64_t received, std::uint64_t total) {
            samples.emplace_back(received, total);
        };

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx", onProgress);

    LOGOS_ASSERT_TRUE(r.ok);
    LOGOS_ASSERT_EQ(samples.size(), std::size_t(2));
    LOGOS_ASSERT_EQ(samples[0].first, std::uint64_t(100));
    LOGOS_ASSERT_EQ(samples[1].first, std::uint64_t(300));
    LOGOS_ASSERT_EQ(samples[1].second, std::uint64_t(4096));
}

LOGOS_TEST(getToFile_ignores_the_progress_of_another_session) {
    std::function<void(const std::string&)> fireDone;
    std::function<void(const std::string&)> fireProgress;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string&) {
            fireProgress(buildProgressPayload("cid-2", 100, 4096));

            fireDone(buildPayload(cid, true));

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    StorageFetcher::OnStorageDownloadProgress onStorageDownloadProgress =
        [&](std::function<void(const std::string&)> callback) {
            fireProgress = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, onStorageDownloadProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    bool reported = false;

    lgpd::ProgressFn onProgress =
        [&](std::uint64_t, std::uint64_t) {
            reported = true;
        };

    fetcher.getToFile("cid-1", "/tmp/wallet.lgx", onProgress);

    LOGOS_ASSERT_FALSE(reported);
}

LOGOS_TEST(getToFile_fetches_the_manifest_before_the_download) {
    std::vector<std::string> calls;
    std::function<void(const std::string&)> fireDone;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string&) {
            calls.push_back("download");
            fireDone(buildPayload(cid, true));
            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    std::function<void(const std::string&)> fireManifest;

    StorageFetcher::OnStorageDownloadManifestDone onManifestDone =
        [&](std::function<void(const std::string&)> callback) {
            fireManifest = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    StorageFetcher::DownloadManifest downloadManifest =
        [&](const std::string& cid) {
            calls.push_back("manifest");
            fireManifest(LogosMap{{"success", true}, {"cid", cid}}.dump());
            return std::string();
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel,
                           downloadManifest, onManifestDone, nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(r.ok);
    LOGOS_ASSERT_EQ(calls.size(), 2u);
    LOGOS_ASSERT_EQ(calls[0], std::string("manifest"));
    LOGOS_ASSERT_EQ(calls[1], std::string("download"));
}

LOGOS_TEST(getToFile_does_not_download_when_the_manifest_fails) {
    bool downloaded = false;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            downloaded = true;
            return std::string();
        };

    std::function<void(const std::string&)> fireManifest;

    StorageFetcher::OnStorageDownloadManifestDone onManifestDone =
        [&](std::function<void(const std::string&)> callback) {
            fireManifest = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    StorageFetcher::DownloadManifest downloadManifest =
        [&](const std::string& cid) {
            fireManifest(LogosMap{{"success", false},
                                  {"cid", cid},
                                  {"error", "no provider"}}.dump());
            return std::string();
        };

    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           downloadManifest, onManifestDone, nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_EQ(r.error, std::string("no provider"));
    LOGOS_ASSERT_FALSE(downloaded);
}

LOGOS_TEST(getToFile_fails_when_the_node_is_not_running) {
    bool downloaded = false;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            downloaded = true;
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(),
                           []() { return false; }, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_EQ(r.error, std::string("the storage node is not running"));
    LOGOS_ASSERT_FALSE(downloaded);
}

LOGOS_TEST(canHandle_accepts_a_url_of_the_node_network) {
    Manifest manifest;
    StorageFetcher fetcher(nullptr, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);

    LOGOS_ASSERT_TRUE(fetcher.canHandle("logos:logos.test:cid-1"));
}

LOGOS_TEST(canHandle_refuses_a_url_of_another_network) {
    Manifest manifest;
    StorageFetcher fetcher(nullptr, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);

    LOGOS_ASSERT_FALSE(fetcher.canHandle("logos:logos.dev:cid-1"));
}

LOGOS_TEST(canHandle_refuses_a_url_when_the_network_is_unknown) {
    Manifest manifest;
    StorageFetcher fetcher(nullptr, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning,
                           []() { return std::string(); }, shortTimeout, shortTimeout);

    LOGOS_ASSERT_FALSE(fetcher.canHandle("logos::cid-1"));
}

LOGOS_TEST(getToFile_downloads_the_cid_of_a_logos_url) {
    std::function<void(const std::string&)> fireDone;
    std::string downloadedCid;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string& cid, const std::string&) {
            downloadedCid = cid;

            const bool success = true;
            fireDone(buildPayload(cid, success));

            return std::string();
        };

    StorageFetcher::OnStorageDownloadDone onStorageDownloadDone =
        [&](std::function<void(const std::string&)> callback) {
            fireDone = std::move(callback);
            return StorageFetcher::Unsubscribe([]() {});
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);
    fetcher.subscribe();

    lgpd::FetchResult r = fetcher.getToFile("logos:logos.test:cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_TRUE(r.ok);
    LOGOS_ASSERT_EQ(downloadedCid, std::string("cid-1"));
}

// A host without storage_module keeps no pending subscription: the impl
// subscribes on the module's first `ready`.
LOGOS_TEST(getToFile_downloads_nothing_before_subscribe) {
    bool downloaded = false;

    StorageFetcher::DownloadToUrl downloadToUrl =
        [&](const std::string&, const std::string&) {
            downloaded = true;
            return std::string();
        };

    Manifest manifest;
    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel,
                           manifest.fetch(), manifest.subscribe(), nodeRunning, network, shortTimeout, shortTimeout);

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_TRUE(r.error.find("not subscribed") != std::string::npos);
    LOGOS_ASSERT_FALSE(downloaded);
}

// The impl calls it on every `ready`.
LOGOS_TEST(subscribe_subscribes_once) {
    int subscriptions = 0;

    auto subscribe = [&](std::function<void(const std::string&)>) {
        ++subscriptions;
        return StorageFetcher::Unsubscribe([]() {});
    };

    StorageFetcher fetcher(nullptr, subscribe, subscribe, unusedCancel,
                           nullptr, subscribe, nodeRunning, network, shortTimeout, shortTimeout);

    fetcher.subscribe();
    fetcher.subscribe();

    LOGOS_ASSERT_EQ(subscriptions, 3);
}
