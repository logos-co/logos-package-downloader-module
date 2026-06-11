// Qt-free test bodies for PackageDownloaderImpl's `logos_events:` methods.

#include <logos_test.h>
#include "package_downloader_impl.h"

using logos_test::recordEvent;

void PackageDownloaderImpl::catalogChanged() { recordEvent("catalogChanged", ""); }
