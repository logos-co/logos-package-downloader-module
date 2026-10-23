#pragma once

#include <functional>
#include <string>
#include <vector>

// Runs inside the mocked downloadPackage, before its progress samples: a test
// can unload the module while a download is in flight.
extern std::function<void()> duringDownloadPackage;

// The transports the mocked downloadPackage tries, in order: each reports a
// 0-byte sample, and the last one completes. {"logos", "http"} plays a
// fallback. Tests that change it put {"http"} back.
extern std::vector<std::string> mockTransports;

namespace lgpd {
enum class DownloadSource;

// The source a newly built lib reads from its config, as if saved by a
// previous run. Tests that change it put Any back.
extern DownloadSource persistedDownloadSource;
}
