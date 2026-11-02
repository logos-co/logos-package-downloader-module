#pragma once

#include "storage_node.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

class StorageFetcher;

// The onReady the impl handed to watchStorageReady: calling it plays a
// storage_module `ready`.
extern std::function<void()> fireStorageReady;

// The onGone the impl handed to watchStorageReady: calling it plays
// storage_module leaving `ready` (unloaded, crashed).
extern std::function<void()> fireStorageGone;

// The callbacks the impl handed to watchStorageNode: calling them plays
// storage_module's storageStart / storageStop with a JSON payload.
extern std::function<void(const std::string&)> fireStorageStarted;
extern std::function<void(const std::string&)> fireStorageStopped;

// The node makeStorageNode hands to the impl.
extern StorageNode fakeStorageNode;

// storage_module subscriptions the fetcher from makeStorageFetcher has taken.
extern int fakeFetcherSubscriptions;

// The last fetcher makeStorageFetcher built: its canHandle plays the lib
// picking a logos: url for a download.
extern std::weak_ptr<StorageFetcher> fakeStorageFetcher;

// The node's network, and how long a download waits for the node to start.
extern std::string fakeStorageNetwork;
extern std::chrono::milliseconds fakeStorageStartTimeout;
