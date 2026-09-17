#include "storage_fetcher_factory.h"

std::shared_ptr<lgpd::Fetcher> makeStorageFetcher(LogosModules&) {
    return nullptr;
}

std::string makeNetwork(LogosModules&) {
    return {};
}

void watchStorageReady(LogosModules&, std::function<void(bool)>) {
}

StorageNode makeStorageNode(LogosModules&) {
    StorageNode node;

    node.state = []() { return std::string("running"); };

    return node;
}
