#include "storage_fetcher.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#include <logos_json.h>

namespace fs = std::filesystem;

StorageFetcher::StorageFetcher(DownloadToUrl downloadToUrl, OnStorageDownloadDone onStorageDownloadDone,
                               OnStorageDownloadProgress onStorageDownloadProgress,
                               DownloadCancel downloadCancel, DownloadManifest downloadManifest,
                               OnStorageDownloadManifestDone onStorageDownloadManifestDone,
                               IsNameKey isNameKey,
                               NameResolve nameResolve,
                               OnStorageNameResolveDone onStorageNameResolveDone,
                               NodeRunning nodeRunning,
                               Network network,
                               std::chrono::milliseconds stallTimeout,
                               std::chrono::milliseconds manifestTimeout,
                               std::chrono::milliseconds resolveTimeout,
                               AwaitNode awaitNode,
                               std::chrono::milliseconds startTimeout)
    : m_downloadToUrl(std::move(downloadToUrl))
    , m_downloadCancel(std::move(downloadCancel))
    , m_downloadManifest(std::move(downloadManifest))
    , m_isNameKey(std::move(isNameKey))
    , m_nameResolve(std::move(nameResolve))
    , m_onStorageDownloadDone(std::move(onStorageDownloadDone))
    , m_onStorageDownloadProgress(std::move(onStorageDownloadProgress))
    , m_onStorageDownloadManifestDone(std::move(onStorageDownloadManifestDone))
    , m_onStorageNameResolveDone(std::move(onStorageNameResolveDone))
    , m_nodeRunning(std::move(nodeRunning))
    , m_network(std::move(network))
    , m_awaitNode(std::move(awaitNode))
    , m_stallTimeout(stallTimeout)
    , m_manifestTimeout(manifestTimeout)
    , m_resolveTimeout(resolveTimeout)
    , m_startTimeout(startTimeout)
{
}

StorageFetcher::~StorageFetcher() {
    std::vector<Unsubscribe> unsubscribes;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        unsubscribes = {m_unsubscribeDone, m_unsubscribeProgress, m_unsubscribeManifest,
                        m_unsubscribeNameResolve};
    }

    // Outside the lock: a cancel waits for a callback in flight, which takes it.
    for (const Unsubscribe& unsubscribe : unsubscribes) {
        if (unsubscribe) {
            unsubscribe();
        }
    }
}

void StorageFetcher::subscribe() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_subscribed) {
            return;
        }

        m_subscribed = true;
    }

    // Outside the lock: an event can arrive while subscribing, and its callback takes it.
    Unsubscribe done = m_onStorageDownloadDone([this](const std::string& payload) {
        onDownloadDone(payload);
    });

    Unsubscribe progress = m_onStorageDownloadProgress([this](const std::string& payload) {
        onDownloadProgress(payload);
    });

    Unsubscribe manifest = m_onStorageDownloadManifestDone([this](const std::string& payload) {
        onManifestDone(payload);
    });

    Unsubscribe nameResolve = m_onStorageNameResolveDone([this](const std::string& payload) {
        onNameResolveDone(payload);
    });

    std::lock_guard<std::mutex> lock(m_mutex);

    m_unsubscribeDone = std::move(done);
    m_unsubscribeProgress = std::move(progress);
    m_unsubscribeManifest = std::move(manifest);
    m_unsubscribeNameResolve = std::move(nameResolve);
}

void StorageFetcher::cancelPendingDownloads(const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_cancelled = reason;

    for (auto& [cid, pending] : m_pending) {
        pending.result.set_value({false, reason});
    }

    for (auto& [cid, manifest] : m_pendingManifests) {
        manifest.set_value({false, reason});
    }

    for (auto& [nameKey, resolve] : m_pendingResolves) {
        resolve.set_value({{false, reason}, {}});
    }

    m_pending.clear();
    m_pendingManifests.clear();
    m_pendingResolves.clear();
}

lgpd::FetchResult StorageFetcher::fetchManifest(const std::string& cid) {
    std::future<lgpd::FetchResult> done;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_unsubscribeManifest) {
            return {false, "not subscribed to storage_module's storageDownloadManifestDone event"};
        }

        if (!m_cancelled.empty()) {
            return {false, m_cancelled};
        }

        if (m_pendingManifests.count(cid) > 0) {
            return {false, "a manifest fetch for " + cid + " is already in progress"};
        }

        done = m_pendingManifests[cid].get_future();
    }

    if (std::string err = m_downloadManifest(cid); !err.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingManifests.erase(cid);
        return {false, std::move(err)};
    }

    if (done.wait_for(m_manifestTimeout) != std::future_status::ready) {
        bool dropped = false;
        {
            // Get a mutex for m_pending
            std::lock_guard<std::mutex> lock(m_mutex);

            // We double check to make sure that the download is still pending and
            // wasn't completed while we were waiting for the lock.
            dropped = m_pendingManifests.erase(cid) > 0;
        }

        if (dropped) {
            return {false, "timed out waiting for the manifest of " + cid};
        }
    }

    return done.get();
}

lgpd::FetchResult StorageFetcher::resolveName(const std::string& nameKey, std::string& cid) {
    std::future<Resolved> done;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_unsubscribeNameResolve) {
            return {false, "not subscribed to storage_module's storageNameResolveDone event"};
        }

        if (!m_cancelled.empty()) {
            return {false, m_cancelled};
        }

        if (m_pendingResolves.count(nameKey) > 0) {
            return {false, "a resolve of " + nameKey + " is already in progress"};
        }

        done = m_pendingResolves[nameKey].get_future();
    }

    if (std::string err = m_nameResolve(nameKey); !err.empty()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingResolves.erase(nameKey);
        return {false, "cannot resolve " + nameKey + ": " + err};
    }

    if (done.wait_for(m_resolveTimeout) != std::future_status::ready) {
        bool dropped = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // The event can arrive while we wait for the lock.
            dropped = m_pendingResolves.erase(nameKey) > 0;
        }

        if (dropped) {
            return {false, "no peer replied for " + nameKey};
        }
    }

    Resolved resolved = done.get();

    if (!resolved.result.ok) {
        return resolved.result;
    }

    cid = resolved.cid;

    return {true, {}};
}

lgpd::FetchResult StorageFetcher::get(const std::string& cid, std::string& out) {
    const fs::path tmp = fs::temp_directory_path() / ("lgpd-storage-" + cid);
    const lgpd::FetchResult fetched = getToFile(cid, tmp.string());

    std::error_code ec;

    if (!fetched.ok) {
        fs::remove(tmp, ec);
        return fetched;
    }

    // Declare a file stream to read the temporary file in binary mode
    std::ifstream file(tmp, std::ios::binary);

    if (!file.is_open()) {
        fs::remove(tmp, ec);
        return {false, "cannot read " + tmp.string()};
    }

    // Read the entire file content into the out string
    std::ostringstream content;
    content << file.rdbuf();
    out = content.str();

    // Close and remove the temporary file
    file.close();
    fs::remove(tmp, ec);

    return {true, {}};
}

bool StorageFetcher::canHandle(const std::string& url) const {
    if (url.rfind("logos:", 0) != 0) {
        return false;
    }

    // Check the network before starting the node.
    const std::string network = m_network();
    const bool handled = !network.empty() && url.rfind("logos:" + network + ":", 0) == 0;

    if (!handled) {
        fprintf(stderr, "StorageFetcher: skipping %s: the storage network is '%s'\n",
                url.c_str(), network.empty() ? "<unknown>" : network.c_str());
        return false;
    }

    // The node starts on the first download that needs it.
    if (m_awaitNode) {
        if (const std::string error = m_awaitNode(m_startTimeout); !error.empty()) {
            fprintf(stderr, "StorageFetcher: skipping %s: %s\n", url.c_str(), error.c_str());
            return false;
        }
    }

    return true;
}

std::string StorageFetcher::network() const {
    return m_network();
}

lgpd::FetchResult StorageFetcher::getToFile(const std::string& url, const std::string& path) {
    return getToFile(url, path, lgpd::ProgressFn{});
}

lgpd::FetchResult StorageFetcher::getToFile(const std::string& url, const std::string& path,
                                            const lgpd::ProgressFn& onProgress) {
    lgpd::FetchResult result = fetchToFile(url, path, onProgress);

    if (!result.ok) {
        fprintf(stderr, "StorageFetcher: storage download of %s failed: %s\n", url.c_str(),
                result.error.c_str());
    }

    return result;
}

lgpd::FetchResult StorageFetcher::fetchToFile(const std::string& url, const std::string& path,
                                              const lgpd::ProgressFn& onProgress) {
    // url is logos:<network>:<key>, the network was checked by canHandle.
    const std::string key = url.substr(url.rfind(':') + 1);

    if (!m_nodeRunning()) {
        return {false, "the storage node is not running"};
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_unsubscribeDone) {
            return {false, "not subscribed to storage_module's storageDownloadDone event"};
        }
    }

    // The key is a dataset CID, or a name key that points to one.
    std::string cid = key;
    bool nameKey = false;

    if (std::string err = m_isNameKey(key, nameKey); !err.empty()) {
        return {false, "cannot check " + key + ": " + err};
    }

    if (nameKey) {
        if (lgpd::FetchResult resolved = resolveName(key, cid); !resolved.ok) {
            return resolved;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_pending.count(cid) > 0) {
            return {false, "a download of " + cid + " is already in progress"};
        }
    }

    // This is important to fetch the manifest before downloading the content.
    // The `fetchManifest` is async and will wait until the manifest retry mechanism
    // is exhausted (up to 10 times) or the timeout is reached.
    //
    // The manifest is needed anyway to do the download but this mechanism is not supported
    // by downloadToUrl, it relies on timeout.
    if (lgpd::FetchResult manifest = fetchManifest(cid); !manifest.ok) {
        return manifest;
    }

    std::future<lgpd::FetchResult> done;
    {
        // Get a mutex for m_pending
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_cancelled.empty()) {
            return {false, m_cancelled};
        }

        if (m_pending.count(cid) > 0) {
            return {false, "a download of " + cid + " is already in progress"};
        }

        Pending& pending = m_pending[cid];

        done = pending.result.get_future();
        pending.onProgress = onProgress;
        pending.lastProgress = std::chrono::steady_clock::now();

        // mutex is released here when lock goes out of scope
    }

    if (std::string err = m_downloadToUrl(cid, path); !err.empty()) {
        // Get a mutex for m_pending
        std::lock_guard<std::mutex> lock(m_mutex);

        m_pending.erase(cid);

        return {false, std::move(err)};
    }

    bool stalled = false;

    while (!stalled) {
        std::chrono::steady_clock::time_point deadline;
        {
            // Get a mutex for m_pending
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_pending.find(cid);

            // The done event erased it: the result is in the future.
            if (it == m_pending.end()) {
                break;
            }

            deadline = it->second.lastProgress + m_stallTimeout;

            if (std::chrono::steady_clock::now() >= deadline) {
                m_pending.erase(it);
                stalled = true;
                break;
            }
        }

        if (done.wait_until(deadline) == std::future_status::ready) {
            break;
        }
    }

    if (stalled) {
        std::string error = "timed out waiting for data for the download of " + cid;

        if (std::string cancelError = m_downloadCancel(cid); !cancelError.empty()) {
            error += " (cancel failed: " + cancelError + ")";
        }

        return {false, error};
    }

    return done.get();
}

void StorageFetcher::onDownloadDone(const std::string& payload) {
    std::string cid;
    lgpd::FetchResult result;

    // The whole read is fenced, not just the parse: a payload that is
    // not an object, or a field of the wrong type, throws too — and we
    // are on the module's event thread, where an escaping exception
    // takes down more than this download.
    try {
        const LogosMap event = LogosMap::parse(payload);

        if (!event.is_object()) {
            return;
        }

        cid = event.value("sessionId", "");

        if (event.value("success", false)) {
            result = {true, {}};
        } else {
            result = {false, event.value("error", "storage download failed")};
        }
    } catch (...) {
        return;
    }

    if (cid.empty()) {
        // Should never happen.
        return;
    }

    // Get a mutex for m_pending
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pending.count(cid) == 0) {
        return;
    }

    m_pending[cid].result.set_value(std::move(result));
    m_pending.erase(cid);
}

void StorageFetcher::onManifestDone(const std::string& payload) {
    std::string cid;
    lgpd::FetchResult result;

    try {
        const LogosMap event = LogosMap::parse(payload);

        if (!event.is_object()) {
            return;
        }

        cid = event.value("cid", "");

        if (event.value("success", false)) {
            result = {true, {}};
        } else {
            result = {false, event.value("error", "storage manifest fetch failed")};
        }
    } catch (...) {
        return;
    }

    if (cid.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_pendingManifests.find(cid);
    if (it == m_pendingManifests.end()) {
        return;
    }

    it->second.set_value(std::move(result));
    m_pendingManifests.erase(it);
}

void StorageFetcher::onNameResolveDone(const std::string& payload) {
    std::string nameKey;
    Resolved resolved;

    try {
        const LogosMap event = LogosMap::parse(payload);

        if (!event.is_object()) {
            return;
        }

        nameKey = event.value("nameKey", "");

        if (!event.value("success", false)) {
            resolved.result = {false, "cannot resolve " + nameKey + ": " +
                                      event.value("error", "storage name resolve failed")};
        } else {
            // Success with an empty cid: the DHT has no name record.
            resolved.cid = event.value("cid", "");

            if (resolved.cid.empty()) {
                resolved.result = {false, "no name record for " + nameKey};
            } else {
                resolved.result = {true, {}};
            }
        }
    } catch (...) {
        return;
    }

    if (nameKey.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // The event of a resolve that timed out can answer a later resolve of the
    // same key, as for manifests: its cid is still valid.
    auto it = m_pendingResolves.find(nameKey);
    if (it == m_pendingResolves.end()) {
        return;
    }

    it->second.set_value(std::move(resolved));
    m_pendingResolves.erase(it);
}

void StorageFetcher::onDownloadProgress(const std::string& payload) {
    std::string cid;
    std::uint64_t bytes = 0;
    std::uint64_t total = 0;

    try {
        const LogosMap event = LogosMap::parse(payload);

        if (!event.is_object()) {
            return;
        }

        cid = event.value("sessionId", "");
        bytes = event.value("bytes", std::uint64_t{0});
        total = event.value("total", std::uint64_t{0});
    } catch (...) {
        return;
    }

    if (cid.empty()) {
        return;
    }

    // Get a mutex for m_pending
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pending.count(cid) == 0) {
        return;
    }

    Pending& pending = m_pending[cid];

    pending.lastProgress = std::chrono::steady_clock::now();

    if (!pending.onProgress) {
        return;
    }

    pending.received += bytes;

    // Called under the lock because pending could be
    // modified concurrently by onDownloadDone.
    //
    // It should not hold the lock for long: the lib throttles
    // to one emit per 200ms and drops the rest on entry.
    pending.onProgress(pending.received, total);
}
