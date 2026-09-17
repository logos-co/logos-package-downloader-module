#pragma once

#include "storage_node.h"

#include <functional>
#include <memory>
#include <string>

namespace lgpd { class Fetcher; }

// Generated per module in logos_sdk.h.
struct LogosModules;

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules& modules);

std::string makeNetwork(LogosModules& modules);

void watchStorageReady(LogosModules& modules, std::function<void(bool)> onChange);

// The configuration comes from ~/.logos_storage/config.json,
// shared with the Storage UI.
StorageNode makeStorageNode(LogosModules& modules);
