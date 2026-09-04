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
                               DownloadCancel downloadCancel, std::chrono::milliseconds downloadTimeout)
    : m_downloadToUrl(std::move(downloadToUrl))
    , m_downloadCancel(std::move(downloadCancel))
    , m_downloadTimeout(downloadTimeout)
{
    m_subscribed = onStorageDownloadDone([this](const std::string& payload) {
        onDownloadDone(payload);
    });

    onStorageDownloadProgress([this](const std::string& payload) {
        onDownloadProgress(payload);
    });
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
