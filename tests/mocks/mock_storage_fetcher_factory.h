#pragma once

#include "storage_node.h"

#include <functional>

// The onReady the impl handed to watchStorageReady: calling it plays a
// storage_module `ready`.
extern std::function<void()> fireStorageReady;

// The node makeStorageNode hands to the impl.
extern StorageNode fakeStorageNode;

// storage_module subscriptions the fetcher from makeStorageFetcher has taken.
extern int fakeFetcherSubscriptions;

// The onChange the impl handed to watchStorageNode: calling it plays a
// storage node start or stop.
extern std::function<void()> fireStorageNodeChanged;

// Calls to watchStorageNode.
extern int fakeNodeWatches;
