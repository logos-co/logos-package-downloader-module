#include "storage_fetcher.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include <logos_json.h>

namespace fs = std::filesystem;

StorageFetcher::StorageFetcher(DownloadToUrl downloadToUrl, OnStorageDownloadDone onStorageDownloadDone,
                               OnStorageDownloadProgress onStorageDownloadProgress,
                               DownloadCancel downloadCancel, DownloadManifest downloadManifest,
                               OnStorageDownloadManifestDone onStorageDownloadManifestDone,
                               std::chrono::milliseconds downloadTimeout,
                               std::chrono::milliseconds manifestTimeout)
    : m_downloadToUrl(std::move(downloadToUrl))
    , m_downloadCancel(std::move(downloadCancel))
    , m_downloadManifest(std::move(downloadManifest))
    , m_downloadTimeout(downloadTimeout)
    , m_manifestTimeout(manifestTimeout)
{
    m_subscribed = onStorageDownloadDone([this](const std::string& payload) {
        onDownloadDone(payload);
    });

    onStorageDownloadProgress([this](const std::string& payload) {
        onDownloadProgress(payload);
    });

    m_manifestSubscribed = onStorageDownloadManifestDone([this](const std::string& payload) {
        onManifestDone(payload);
    });
}

lgpd::FetchResult StorageFetcher::fetchManifest(const std::string& cid) {
    if (!m_manifestSubscribed) {
        return {false, "not subscribed to storage_module's storageDownloadManifestDone event"};
    }

    std::future<lgpd::FetchResult> done;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

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

lgpd::FetchResult StorageFetcher::getToFile(const std::string& cid, const std::string& path) {
    return getToFile(cid, path, lgpd::ProgressFn{});
}

lgpd::FetchResult StorageFetcher::getToFile(const std::string& cid, const std::string& path,
                                            const lgpd::ProgressFn& onProgress) {
    if (!m_subscribed) {
        return {false, "not subscribed to storage_module's storageDownloadDone event"};
    }

    // This is important to fetch the manifest before downloading the content.
    // The `fetchManifest` is async and will wait until the manifest retry mechanism
    // is exhausted (up to 10 times).
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

        if (m_pending.count(cid) > 0) {
            return {false, "a download of " + cid + " is already in progress"};
        }

        Pending& pending = m_pending[cid];

        done = pending.result.get_future();
        pending.onProgress = onProgress;

        // mutex is released here when lock goes out of scope
    }

    if (std::string err = m_downloadToUrl(cid, path); !err.empty()) {
        // Get a mutex for m_pending
        std::lock_guard<std::mutex> lock(m_mutex);

        m_pending.erase(cid);

        return {false, std::move(err)};
    }

    if (done.wait_for(m_downloadTimeout) != std::future_status::ready) {
        bool dropped = false;
        {
            // Get a mutex for m_pending
            std::lock_guard<std::mutex> lock(m_mutex);

            // We double check to make sure that the download is still pending and
            // wasn't completed while we were waiting for the lock.
            dropped = m_pending.erase(cid) > 0;

            // mutex is released here when lock goes out of scope
        }

        if (dropped) {
            std::string error = "timed out waiting for the download of " + cid;

            if (std::string cancelError = m_downloadCancel(cid); !cancelError.empty()) {
                error += " (cancel failed: " + cancelError + ")";
            }

            return {false, error};
        }
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

    if (!pending.onProgress) {
        return;
    }

    pending.received += bytes;

    // Called under the lock because pending could be
    // modified concurrently by onDownloadDone.
    pending.onProgress(pending.received, total);
}
