#include "storage_node.h"

#include <cstdio>
#include <string>

std::string startStorageNode(const StorageNode& node, bool& owned) {
    const std::string state = node.state();

    if (state.empty()) {
        return "the storage module state is unknown";
    }

    if (state == "running" || state == "starting") {
        return {};
    }

    if (state == "destroyed") {
        std::string error;
        const std::string config = node.migrateConfig(error);

        if (!error.empty()) {
            return error;
        }

        if (!node.init(config)) {
            return "the storage module refused the configuration";
        }

        owned = true;
    }

    if (!node.start()) {
        return "the storage module refused the start command";
    }

    return {};
}

void stopStorageNode(const StorageNode& node, std::function<void()> onDone) {
    // The callback carries its own copy of destroy: it fires on the storageStop
    // event, long after this call returned.
    node.onStopped([destroy = node.destroy, onDone](bool stopped) {
        if (stopped) {
            destroy();
        } else {
            fprintf(stderr, "storage node: the module failed to stop it, not destroying it\n");
        }

        onDone();
    });

    if (!node.stop()) {
        fprintf(stderr, "storage node: the module refused the stop command\n");
        onDone();
    }
}
