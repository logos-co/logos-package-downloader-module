#include "storage_node.h"

#include <string>

std::string startStorageNode(const StorageNode& node) {
    if (node.isRunning()) {
        return {};
    }

    std::string error;
    const std::string config = node.migrateConfig(error);

    if (!error.empty()) {
        return error;
    }

    // If the init fails it might mean 2 different things:
    // 1. Real failure
    // 2. The context was created by another consumer
    //
    // If it is a real failure, the start command just below will fail
    // and return an error.
    //
    // If the context was created by another consumer, the start command
    // will succeed and the node will start if it is not already running.
    node.init(config);

    // If the start fails we check if the node is running, to distinguish
    // between a real failure and a node started by another consumer.
    if (!node.start() && !node.isRunning()) {
        return "the storage module refused the start command";
    }

    return {};
}
