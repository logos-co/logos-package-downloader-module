#pragma once

#include <functional>
#include <string>

// Each call answers through its callback, once storage_module replies.
struct StorageNode {
    std::function<void(std::function<void(bool running)>)> isRunning;

    // The configuration to hand to init(), or an empty one and the reason in
    // `error` when it could not be obtained.
    std::function<void(std::function<void(const std::string& config, const std::string& error)>)> loadConfig;

    std::function<void(const std::string& config, std::function<void(bool accepted)>)> init;
    std::function<void(std::function<void(bool accepted)>)> start;
};

// Bring the node up: load the configuration, init and start. `done` gets
// an empty string on success, the reason otherwise.
void startStorageNode(const StorageNode& node, std::function<void(const std::string& error)> done);
