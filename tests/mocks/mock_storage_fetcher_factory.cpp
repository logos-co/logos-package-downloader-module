#include "storage_fetcher_factory.h"
#include "mock_storage_fetcher_factory.h"

#include <utility>

std::function<void()> fireStorageReady;

// By default both calls succeed and touch nothing.
StorageNode fakeStorageNode = []() {
    StorageNode node;

    node.isRunning = []() { return false; };
    node.loadConfig = [](std::string&) { return std::string("{}"); };
    node.init = [](const std::string&) { return true; };
    node.start = []() { return true; };

    return node;
}();

std::shared_ptr<StorageFetcher> makeStorageFetcher(LogosModules&) {
    return nullptr;
}

std::string makeNetwork(LogosModules&) {
    return {};
}

std::function<void()> watchStorageReady(LogosModules&, std::function<void()> onReady) {
    fireStorageReady = std::move(onReady);
    return {};
}

StorageNode makeStorageNode(LogosModules&) {
    return fakeStorageNode;
}
