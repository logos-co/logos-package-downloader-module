#include "storage_fetcher_factory.h"

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules&) {
    return nullptr;
}

std::string makeNetwork(LogosModules&) {
    return {};
}

std::function<void()> watchStorageReady(LogosModules&, std::function<void(bool)>) {
    return {};
}

StorageNode makeStorageNode(LogosModules&) {
    StorageNode node;

    node.state = []() { return std::string("running"); };

    return node;
}
