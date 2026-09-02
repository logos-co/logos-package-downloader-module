// Qt-free test bodies for PackageDownloaderImpl's `logos_events:` methods.

#include <logos_test.h>
#include "package_downloader_impl.h"

#include <string>

using logos_test::recordEvent;

void PackageDownloaderImpl::catalogChanged() { recordEvent("catalogChanged", ""); }

// The capture sink carries a single string, so the typed args are flattened
// to "<package>:<received>/<total>" — enough for tests to assert both the
// attribution and the byte counts.
void PackageDownloaderImpl::downloadProgress(const std::string& packageName,
                                             uint64_t received, uint64_t total) {
    recordEvent("downloadProgress", packageName + ":" + std::to_string(received)
                                    + "/" + std::to_string(total));
}
