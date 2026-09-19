#include "storage_fetcher_factory.h"

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules&) {
    return nullptr;
}

std::string makeNetwork(LogosModules&) {
    return {};
}

std::function<void()> watchStorageReady(LogosModules&, std::function<void()>) {
    return {};
}

// The node the impl tests never look at: both calls succeed and touch nothing.
StorageNode makeStorageNode(LogosModules&) {
    StorageNode node;

    node.isRunning = []() { return false; };
    node.migrateConfig = [](std::string&) { return std::string("{}"); };
    node.init = [](const std::string&) { return true; };
    node.start = []() { return true; };

    return node;
}
