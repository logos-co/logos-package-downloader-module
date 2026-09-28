#pragma once

#include <functional>

// Runs inside the mocked downloadPackage, before its progress samples: a test
// can unload the module while a download is in flight.
extern std::function<void()> duringDownloadPackage;
