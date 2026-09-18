#include "storage_node.h"

#include <string>

std::string startStorageNode(const StorageNode& node) {
    std::string error;
    const std::string config = node.migrateConfig(error);

    if (!error.empty()) {
        return error;
    }

    if (!node.init(config)) {
        return "the storage module refused the configuration";
    }

    if (!node.start()) {
        return "the storage module refused the start command";
    }

    return {};
}
