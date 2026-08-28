#pragma once

#include <chrono>
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
    // Returns an empty string when the session was dropped, an error
    // message otherwise.
    using DownloadCancel = std::function<std::string(const std::string& cid)>;

    // Default timeout is 5 minutes.
    StorageFetcher(DownloadToUrl downloadToUrl, OnStorageDownloadDone onStorageDownloadDone, DownloadCancel downloadCancel, std::chrono::milliseconds downloadTimeout = std::chrono::minutes(5));

    lgpd::FetchResult get(const std::string& cid, std::string& out) override;
    lgpd::FetchResult getToFile(const std::string& cid, const std::string& path) override;

private:
    void onDownloadDone(const std::string& payload);

    DownloadToUrl m_downloadToUrl;
    DownloadCancel m_downloadCancel;
    std::chrono::milliseconds m_downloadTimeout;
    bool m_subscribed = false;

    std::mutex m_mutex;
    std::map<std::string, std::promise<lgpd::FetchResult>> m_pending;
};
