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

// onReady when storage_module becomes ready, onGone when it stops being so
// (unloaded, crashed). Returns the cancel.
std::function<void()> watchStorageReady(LogosModules& modules, std::function<void()> onReady, std::function<void()> onGone = {});

// storage_module's storageStart and storageStop, with their JSON payloads
// ({"success": bool, "message": string}). Returns the cancel.
std::function<void()> watchStorageNode(LogosModules& modules, std::function<void(const std::string&)> onStarted, std::function<void(const std::string&)> onStopped);

// The configuration is the one the storage module saved at its last init,
// or its default.
StorageNode makeStorageNode(LogosModules& modules);
