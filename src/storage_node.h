#pragma once

#include <functional>
#include <string>

struct StorageNode {
    std::function<bool()> isRunning;

    // The configuration to hand to init(). Returns an empty string
    // and the reason in `error` when it could not be obtained.
    std::function<std::string(std::string& error)> loadConfig;

    std::function<bool(const std::string& config)> init;
    std::function<bool()> start;
};

// Bring the node up: load the configuration, init and start. Returns
// an empty string on success, the reason otherwise.
std::string startStorageNode(const StorageNode& node);
