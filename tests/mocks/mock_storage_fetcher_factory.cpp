#include "storage_fetcher_factory.h"
#include "storage_fetcher.h"
#include "mock_storage_fetcher_factory.h"

#include <string>
#include <utility>

std::function<void()> fireStorageReady;

int fakeFetcherSubscriptions = 0;

// By default both calls succeed and touch nothing.
StorageNode fakeStorageNode = []() {
    StorageNode node;

    node.isRunning = [](std::function<void(bool)> done) { done(false); };
    node.loadConfig = [](std::function<void(const std::string&, const std::string&)> done) { done("{}", ""); };
    node.init = [](const std::string&, std::function<void(bool)> done) { done(true); };
    node.start = [](std::function<void(bool)> done) { done(true); };

    return node;
}();

// A real fetcher over a storage_module that is never running.
std::shared_ptr<StorageFetcher> makeStorageFetcher(LogosModules&) {
    auto subscribe = [](std::function<void(const std::string&)>) {
        ++fakeFetcherSubscriptions;
        return StorageFetcher::Unsubscribe([]() {});
    };

    return std::make_shared<StorageFetcher>(
        nullptr, subscribe, subscribe, nullptr, nullptr, subscribe,
        []() { return false; }, []() { return std::string(); });
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
