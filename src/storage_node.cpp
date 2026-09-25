#include "storage_node.h"

#include <string>

void startStorageNode(const StorageNode& node, std::function<void(const std::string& error)> done) {
    node.isRunning([node, done](bool running) {
        if (running) {
            done({});
            return;
        }

        node.loadConfig([node, done](const std::string& config, const std::string& error) {
            if (!error.empty()) {
                done(error);
                return;
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
            node.init(config, [node, done](bool) {
                node.start([node, done](bool accepted) {
                    if (accepted) {
                        done({});
                        return;
                    }

                    // If the start fails we check if the node is running, to distinguish
                    // between a real failure and a node started by another consumer.
                    node.isRunning([done](bool running) {
                        done(running ? std::string() : "the storage module refused the start command");
                    });
                });
            });
        });
    });
}
