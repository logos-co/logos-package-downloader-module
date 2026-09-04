#include <logos_test.h>
#include <logos_json.h>
#include "storage_fetcher.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
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
    [](std::function<void(const std::string&)>) { return true; };

const StorageFetcher::OnStorageDownloadProgress unusedProgress =
    [](std::function<void(const std::string&)>) { return true; };

const StorageFetcher::DownloadCancel unusedCancel =
    [](const std::string&) {
        return std::string();
    };

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
            return true;
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel);

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
            return true;
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel);

    lgpd::FetchResult r = fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_FALSE(r.ok);
    LOGOS_ASSERT_EQ(r.error, std::string("cid not found"));
}

LOGOS_TEST(getToFile_returns_the_error_when_the_downloadToUrl_returns_an_error) {
    StorageFetcher::DownloadToUrl downloadToUrl =
        [](const std::string&, const std::string&) {
            return std::string("node not started");
        };

    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel);

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
            return false;
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel);

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
            return true;
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, unusedProgress, unusedCancel);
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

    const std::chrono::milliseconds downloadTimeout(50);

    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, unusedCancel, downloadTimeout);

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

    const std::chrono::milliseconds downloadTimeout(50);

    StorageFetcher fetcher(downloadToUrl, unusedDone, unusedProgress, downloadCancel, downloadTimeout);

    fetcher.getToFile("cid-1", "/tmp/wallet.lgx");

    LOGOS_ASSERT_EQ(cancelledCid, std::string("cid-1"));
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
            return true;
        };

    StorageFetcher::OnStorageDownloadProgress onStorageDownloadProgress =
        [&](std::function<void(const std::string&)> callback) {
            fireProgress = std::move(callback);
            return true;
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, onStorageDownloadProgress, unusedCancel);

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
            return true;
        };

    StorageFetcher::OnStorageDownloadProgress onStorageDownloadProgress =
        [&](std::function<void(const std::string&)> callback) {
            fireProgress = std::move(callback);
            return true;
        };

    StorageFetcher fetcher(downloadToUrl, onStorageDownloadDone, onStorageDownloadProgress, unusedCancel);

    bool reported = false;

    lgpd::ProgressFn onProgress =
        [&](std::uint64_t, std::uint64_t) {
            reported = true;
        };

    fetcher.getToFile("cid-1", "/tmp/wallet.lgx", onProgress);

    LOGOS_ASSERT_FALSE(reported);
}
