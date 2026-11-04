#pragma once

#include <functional>
#include <string>

#include <logos_result.h>

// Each call answers through its callback, once storage_module replies.
struct StorageNode {
    std::function<void(std::function<void(bool running)>)> isRunning;

    // The configuration to hand to init(), or an empty one and the reason in
    // `error` when it could not be obtained.
    std::function<void(std::function<void(const std::string& config, const std::string& error)>)> loadConfig;

    std::function<void(const std::string& config, std::function<void(bool accepted)>)> init;
    std::function<void(std::function<void(bool accepted)>)> start;

    // Optional: true once the caller is unloading. The start then makes no
    // further call to storage_module.
    std::function<bool()> stopped;
};

// Bring the node up: load the configuration, init and start. `done` gets
// an empty string on success, the reason otherwise.
void startStorageNode(const StorageNode& node, std::function<void(const std::string& error)> done);

// The `network` of a storage configuration.
// `logos.test` when `network` is missing: the storage node's default.
// empty when the configuration is not a JSON object, or when `network` is not a string.
std::string configNetwork(const std::string& config);

// The storage network: empty while storage_module is not ready, otherwise
// the configNetwork of the configuration `loadConfig` returns.
// `loadConfig` is called only when storage_module is ready.
std::string storageNetwork(bool storageReady, const std::function<StdLogosResult()>& loadConfig);
