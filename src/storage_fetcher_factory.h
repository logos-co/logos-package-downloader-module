#pragma once

#include "storage_node.h"

#include <functional>
#include <memory>
#include <string>

class StorageFetcher;

// Generated per module in logos_sdk.h.
struct LogosModules;

std::shared_ptr<StorageFetcher> makeStorageFetcher(LogosModules& modules);

std::string makeNetwork(LogosModules& modules);

std::function<void()> watchStorageReady(LogosModules& modules, std::function<void()> onReady);

// The configuration is the one the storage module saved at its last init,
// or its default.
StorageNode makeStorageNode(LogosModules& modules);
