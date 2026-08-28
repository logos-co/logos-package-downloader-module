#include "storage_fetcher.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include <logos_json.h>

namespace fs = std::filesystem;

StorageFetcher::StorageFetcher(DownloadToUrl downloadToUrl, OnStorageDownloadDone onStorageDownloadDone,
                               std::chrono::milliseconds downloadTimeout)
    : m_downloadToUrl(std::move(downloadToUrl))
    , m_downloadTimeout(downloadTimeout)
{
    m_subscribed = onStorageDownloadDone([this](const std::string& payload) {
        onDownloadDone(payload);
    });
}

lgpd::FetchResult StorageFetcher::get(const std::string& cid, std::string& out) {
    const fs::path tmp = fs::temp_directory_path() / ("lgpd-storage-" + cid);
    const lgpd::FetchResult fetched = getToFile(cid, tmp.string());

    if (!fetched.ok) {
        return fetched;
    }

    std::error_code ec;

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

        done = m_pending[cid].get_future();

        // mutex is released here when lock goes out of scope
    }

    if (std::string err = m_downloadToUrl(cid, path); !err.empty()) {
        // Get a mutex for m_pending
        std::lock_guard<std::mutex> lock(m_mutex);

        m_pending.erase(cid);

        return {false, std::move(err)};
    }

    if (done.wait_for(m_downloadTimeout) != std::future_status::ready) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Just in case the download finished while we were waiting for the lock.
        if (m_pending.count(cid) > 0) {
            m_pending.erase(cid);
            return {false, "timed out waiting for the download of " + cid};
        }
    }

    return done.get();
}

void StorageFetcher::onDownloadDone(const std::string& payload) {
    LogosMap event;

    try {
        event = LogosMap::parse(payload);
    } catch (...) {
        return;
    }

    const std::string cid = event.value("sessionId", "");

    if (cid.empty()) {
        // Should never happen.
        return;
    }

    // Get a mutex for m_pending
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pending.count(cid) == 0) {
        return;
    }

    if (event.value("success", false)) {
        m_pending[cid].set_value({true, {}});
    } else {
        m_pending[cid].set_value({false, event.value("error", "storage download failed")});
    }

    m_pending.erase(cid);
}
