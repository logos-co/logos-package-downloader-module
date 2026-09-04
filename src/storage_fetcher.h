#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>

#include <package_downloader_lib.h>

/**
 * lgpd::Fetcher backed by the Logos storage network: fetches an artifact
 * by CID through storage_module.
 */
class StorageFetcher : public lgpd::Fetcher {
public:
    // Extract Storage Module function types to make tests easier.
    using DownloadToUrl = std::function<std::string(const std::string& cid, const std::string& filePath)>;
    using OnStorageDownloadDone = std::function<bool(std::function<void(const std::string& payload)>)>;
    using OnStorageDownloadProgress = std::function<bool(std::function<void(const std::string& payload)>)>;
    // Returns an empty string when the session was dropped, an error
    // message otherwise.
    using DownloadCancel = std::function<std::string(const std::string& cid)>;

    // Default timeout is 5 minutes.
    StorageFetcher(DownloadToUrl downloadToUrl, OnStorageDownloadDone onStorageDownloadDone, OnStorageDownloadProgress onStorageDownloadProgress, DownloadCancel downloadCancel, std::chrono::milliseconds downloadTimeout = std::chrono::minutes(5));

    lgpd::FetchResult get(const std::string& cid, std::string& out) override;
    lgpd::FetchResult getToFile(const std::string& cid, const std::string& path) override;
    lgpd::FetchResult getToFile(const std::string& cid, const std::string& path,
                                const lgpd::ProgressFn& onProgress) override;

private:
    void onDownloadDone(const std::string& payload);
    void onDownloadProgress(const std::string& payload);

    struct Pending {
        std::promise<lgpd::FetchResult> result;
        lgpd::ProgressFn onProgress;
        std::uint64_t received = 0;
    };

    DownloadToUrl m_downloadToUrl;
    DownloadCancel m_downloadCancel;
    std::chrono::milliseconds m_downloadTimeout;
    bool m_subscribed = false;

    std::mutex m_mutex;
    std::map<std::string, Pending> m_pending;
};
