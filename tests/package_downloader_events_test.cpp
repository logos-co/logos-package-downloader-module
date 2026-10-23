// Qt-free test bodies for PackageDownloaderImpl's `logos_events:` methods.

#include <logos_test.h>
#include "package_downloader_impl.h"

#include <string>

using logos_test::recordEvent;

void PackageDownloaderImpl::catalogChanged() { recordEvent("catalogChanged", ""); }

// The capture sink carries a single string, so the typed args are flattened
// to "<package>:<received>/<total>@<transport>" — enough for tests to assert
// the attribution, the byte counts and where the bytes came from.
void PackageDownloaderImpl::downloadProgress(const std::string& packageName,
                                             uint64_t received, uint64_t total,
                                             const std::string& transport) {
    recordEvent("downloadProgress", packageName + ":" + std::to_string(received)
                                    + "/" + std::to_string(total) + "@" + transport);
}

void PackageDownloaderImpl::downloadDone(const std::string& packageName,
                                         const std::string& source) {
    recordEvent("downloadDone", packageName + ":" + source);
}

void PackageDownloaderImpl::stateChanged(const std::string& state) {
    recordEvent("stateChanged", state);
}

void PackageDownloaderImpl::storageStateChanged(const std::string& state,
                                                const std::string& error) {
    recordEvent("storageStateChanged", error.empty() ? state : state + ":" + error);
}
