#pragma once

#include <memory>
#include <string>

namespace lgpd { class Fetcher; }

// Generated per module in logos_sdk.h.
struct LogosModules;

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules& modules);

std::string makeNetwork(LogosModules& modules);
