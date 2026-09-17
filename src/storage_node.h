#pragma once

#include <functional>
#include <string>

struct StorageNode {
    // "destroyed", "stopped", "starting", "running" or "stopping". Empty when
    // the module did not answer.
    std::function<std::string()> state;

    // The migrated configuration to hand to init(). Returns an empty string
    // and the reason in `error` when it could not be obtained.
    std::function<std::string(std::string& error)> migrateConfig;

    std::function<bool(const std::string& config)> init;
    std::function<bool()> start;
    std::function<bool()> stop;
    std::function<void()> destroy;

    // Subscribes to the end of the stop. The callback tells whether the module
    // did stop the node, which is what makes it safe to destroy.
    std::function<bool(std::function<void(bool stopped)>)> onStopped;
};

// Bring the node up: migrate the shared configuration, init and start. Returns
// an empty string on success, the reason otherwise.
std::string startStorageNode(const StorageNode& node, bool& owned);

// Stop the node, then destroy its context once the module reports the stop is
// done — destroying a node still running can cost the repository. `onDone` runs
// on the module's event thread, and runs once even when nothing was stopped.
void stopStorageNode(const StorageNode& node, std::function<void()> onDone);
