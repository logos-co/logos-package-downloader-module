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
    using DownloadCancel = std::function<std::string(const std::string& cid)>;
    using DownloadManifest = std::function<std::string(const std::string& cid)>;
    using OnStorageDownloadManifestDone = std::function<bool(std::function<void(const std::string& payload)>)>;
    using NodeRunning = std::function<bool()>;
    using Network = std::function<std::string()>;

    // Default timeout for the manifest: 5 minutes. A net against a lost event,
    // not the bound of the retry: storage always emits
    // storageDownloadManifestDone, failure included.
    // Default timeout for the transfer: 10 minutes, same as the https fetcher's
    // CURLOPT_TIMEOUT, so storage does not give up before it.
    StorageFetcher(
        DownloadToUrl downloadToUrl,
        OnStorageDownloadDone onStorageDownloadDone,
        OnStorageDownloadProgress onStorageDownloadProgress,
        DownloadCancel downloadCancel,
        DownloadManifest downloadManifest,
        OnStorageDownloadManifestDone onStorageDownloadManifestDone,
        NodeRunning nodeRunning,
        Network network,
        std::chrono::milliseconds downloadTimeout = std::chrono::minutes(10),
        std::chrono::milliseconds manifestTimeout = std::chrono::minutes(5));

    bool canHandle(const std::string& url) const override;
    lgpd::FetchResult get(const std::string& cid, std::string& out) override;
    lgpd::FetchResult getToFile(const std::string& url, const std::string& path) override;
    lgpd::FetchResult getToFile(const std::string& url, const std::string& path,
                                const lgpd::ProgressFn& onProgress) override;

private:
    lgpd::FetchResult fetchManifest(const std::string& cid);

    void onDownloadDone(const std::string& payload);
    void onDownloadProgress(const std::string& payload);
    void onManifestDone(const std::string& payload);

    struct Pending {
        std::promise<lgpd::FetchResult> result;
        lgpd::ProgressFn onProgress;
        std::uint64_t received = 0;
    };

    DownloadToUrl m_downloadToUrl;
    DownloadCancel m_downloadCancel;
    DownloadManifest m_downloadManifest;

    OnStorageDownloadDone m_onStorageDownloadDone;
    OnStorageDownloadProgress m_onStorageDownloadProgress;
    OnStorageDownloadManifestDone m_onStorageDownloadManifestDone;
    NodeRunning m_nodeRunning;
    Network m_network;

    std::chrono::milliseconds m_downloadTimeout;
    std::chrono::milliseconds m_manifestTimeout;

    bool m_subscribed = false;
    bool m_manifestSubscribed = false;

    std::mutex m_mutex;
    std::map<std::string, Pending> m_pending;
    std::map<std::string, std::promise<lgpd::FetchResult>> m_pendingManifests;
};
