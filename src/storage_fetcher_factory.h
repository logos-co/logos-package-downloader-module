#pragma once

#include <memory>

namespace lgpd { class Fetcher; }

// Generated per module in logos_sdk.h.
struct LogosModules;

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules& modules);
