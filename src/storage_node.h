#pragma once

#include <functional>
#include <string>

struct StorageNode {
    // The migrated configuration to hand to init(). Returns an empty string
    // and the reason in `error` when it could not be obtained.
    std::function<std::string(std::string& error)> migrateConfig;

    std::function<bool(const std::string& config)> init;
    std::function<bool()> start;
};

// Bring the node up: migrate the shared configuration, init and start. Returns
// an empty string on success, the reason otherwise. The node is shared, so a
// node someone else already brought up answers both calls without touching it.
std::string startStorageNode(const StorageNode& node);
