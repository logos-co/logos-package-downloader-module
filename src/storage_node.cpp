#include "storage_node.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

std::string startStorageNode(const StorageNode& node, bool& owned) {
    const std::string state = node.state();

    if (state.empty()) {
        return "the storage module state is unknown";
    }

    if (state == "running" || state == "starting" || state == "stopping") {
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
    // Guard to prevent multiple stop calls from firing the callback multiple times.
    auto pending = std::make_shared<std::atomic<bool>>(true);

    // The callback carries its own copy of destroy: it fires on the storageStop
    // event, long after this call returned.
    const bool subscribed = node.onStopped([destroy = node.destroy, onDone, pending](bool stopped) {
        if (!pending->exchange(false)) {
            return;
        }

        if (stopped) {
            destroy();
        } else {
            fprintf(stderr, "storage node: the module failed to stop it, not destroying it\n");
        }

        onDone();
    });

    if (!subscribed) {
        fprintf(stderr, "storage node: the subscription failed, the stop command will not be acknowledged\n");
    }

    const bool stopping = node.stop();

    if (!stopping) {
        fprintf(stderr, "storage node: the module refused the stop command\n");
    }

    if (stopping && subscribed) {
        // The onDone callback will be fired by the subscription.
        return;
    }

    // If another stop call is in progress, it will be in charge of firing the callback.
    if (pending->exchange(false)) {
        onDone();
    }
}
