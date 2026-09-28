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
    using Unsubscribe = std::function<void()>;
    using OnStorageDownloadDone = std::function<Unsubscribe(std::function<void(const std::string& payload)>)>;
    using OnStorageDownloadProgress = std::function<Unsubscribe(std::function<void(const std::string& payload)>)>;
    using DownloadCancel = std::function<std::string(const std::string& cid)>;
    using DownloadManifest = std::function<std::string(const std::string& cid)>;
    using OnStorageDownloadManifestDone = std::function<Unsubscribe(std::function<void(const std::string& payload)>)>;
    using NodeRunning = std::function<bool()>;
    using Network = std::function<std::string()>;

    // Default timeout for the manifest: 60 seconds.
    // The transfer has no total limit: it gives up
    // after 60 seconds without a progress event.
    StorageFetcher(
        DownloadToUrl downloadToUrl,
        OnStorageDownloadDone onStorageDownloadDone,
        OnStorageDownloadProgress onStorageDownloadProgress,
        DownloadCancel downloadCancel,
        DownloadManifest downloadManifest,
        OnStorageDownloadManifestDone onStorageDownloadManifestDone,
        NodeRunning nodeRunning,
        Network network,
        std::chrono::milliseconds stallTimeout = std::chrono::seconds(60),
        std::chrono::milliseconds manifestTimeout = std::chrono::seconds(60));

    ~StorageFetcher() override;

    // Takes the storage_module subscriptions, once. Called on its first
    // `ready`: a host without the module then keeps no pending subscriptions.
    void subscribe();

    void cancelPendingDownloads();

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
        std::chrono::steady_clock::time_point lastProgress;
    };

    DownloadToUrl m_downloadToUrl;
    DownloadCancel m_downloadCancel;
    DownloadManifest m_downloadManifest;

    OnStorageDownloadDone m_onStorageDownloadDone;
    OnStorageDownloadProgress m_onStorageDownloadProgress;
    OnStorageDownloadManifestDone m_onStorageDownloadManifestDone;
    NodeRunning m_nodeRunning;
    Network m_network;

    std::chrono::milliseconds m_stallTimeout;
    std::chrono::milliseconds m_manifestTimeout;

    std::mutex m_mutex;

    // Guarded by m_mutex: subscribe() runs on the main thread, downloads on workers.
    bool m_subscribed = false;
    Unsubscribe m_unsubscribeDone;
    Unsubscribe m_unsubscribeProgress;
    Unsubscribe m_unsubscribeManifest;

    bool m_unloading = false;
    std::map<std::string, Pending> m_pending;
    std::map<std::string, std::promise<lgpd::FetchResult>> m_pendingManifests;
};
