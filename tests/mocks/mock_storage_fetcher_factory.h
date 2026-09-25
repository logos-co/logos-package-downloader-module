#pragma once

#include "storage_node.h"

#include <functional>

// The onReady the impl handed to watchStorageReady: calling it plays a
// storage_module `ready`.
extern std::function<void()> fireStorageReady;

// The node makeStorageNode hands to the impl.
extern StorageNode fakeStorageNode;
