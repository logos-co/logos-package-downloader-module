// The storage node lifecycle, with the storage_module calls replaced by a
// recording fake.

#include <logos_test.h>
#include "storage_node.h"

#include <functional>
#include <string>
#include <utility>

namespace {

struct FakeNode {
    std::string state = "destroyed";
    std::string config = R"({"data-dir":"/tmp/storage"})";
    std::string configError;
    bool initAccepted = true;
    bool startAccepted = true;
    bool stopAccepted = true;

    // The storageStop subscriber the lifecycle installed, fired by the tests.
    std::function<void(bool)> stopped;

    std::string initConfig;
    bool initCalled = false;
    bool startCalled = false;
    bool destroyCalled = false;

    StorageNode node() {
        StorageNode n;

        n.state = [this]() { return state; };

        n.migrateConfig = [this](std::string& error) {
            error = configError;
            return config;
        };

        n.init = [this](const std::string& cfg) {
            initCalled = true;
            initConfig = cfg;
            return initAccepted;
        };

        n.start = [this]() {
            startCalled = true;
            return startAccepted;
        };

        n.stop = [this]() { return stopAccepted; };

        n.destroy = [this]() { destroyCalled = true; };

        n.onStopped = [this](std::function<void(bool)> callback) {
            stopped = std::move(callback);
            return true;
        };

        return n;
    }
};

} // namespace

LOGOS_TEST(start_leaves_a_node_someone_else_runs_alone) {
    FakeNode fake;
    fake.state = "running";
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_FALSE(fake.startCalled);
    LOGOS_ASSERT_FALSE(owned);
}

LOGOS_TEST(start_leaves_a_node_someone_else_is_starting_alone) {
    FakeNode fake;
    fake.state = "starting";
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_FALSE(owned);
}

LOGOS_TEST(start_inits_and_starts_a_destroyed_node) {
    FakeNode fake;
    fake.state = "destroyed";
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_EQ(fake.initConfig, fake.config);
    LOGOS_ASSERT_TRUE(fake.startCalled);
    LOGOS_ASSERT_TRUE(owned);
}

LOGOS_TEST(start_starts_a_stopped_node_without_initialising_it) {
    FakeNode fake;
    fake.state = "stopped";
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_TRUE(fake.startCalled);
    LOGOS_ASSERT_FALSE(owned);
}

LOGOS_TEST(start_reports_a_module_that_does_not_answer) {
    FakeNode fake;
    fake.state = "";
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_EQ(error, std::string("the storage module state is unknown"));
    LOGOS_ASSERT_FALSE(fake.initCalled);
}

LOGOS_TEST(start_reports_a_configuration_that_could_not_be_migrated) {
    FakeNode fake;
    fake.configError = "Invalid configuration: expected a JSON object.";
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_EQ(error, fake.configError);
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_FALSE(owned);
}

LOGOS_TEST(start_reports_a_refused_configuration) {
    FakeNode fake;
    fake.initAccepted = false;
    bool owned = false;

    const std::string error = startStorageNode(fake.node(), owned);

    LOGOS_ASSERT_EQ(error, std::string("the storage module refused the configuration"));
    LOGOS_ASSERT_FALSE(fake.startCalled);
}

LOGOS_TEST(stop_destroys_the_node_once_the_module_reports_it_stopped) {
    FakeNode fake;
    fake.state = "running";
    bool done = false;

    stopStorageNode(fake.node(), [&done]() { done = true; });
    fake.stopped(true);

    LOGOS_ASSERT_TRUE(fake.destroyCalled);
    LOGOS_ASSERT_TRUE(done);
}

LOGOS_TEST(stop_keeps_the_node_when_the_module_did_not_stop_it) {
    FakeNode fake;
    fake.state = "running";
    bool done = false;

    stopStorageNode(fake.node(), [&done]() { done = true; });
    fake.stopped(false);

    LOGOS_ASSERT_FALSE(fake.destroyCalled);
    LOGOS_ASSERT_TRUE(done);
}

LOGOS_TEST(stop_finishes_the_unload_when_the_stop_is_refused) {
    FakeNode fake;
    fake.state = "running";
    fake.stopAccepted = false;
    bool done = false;

    stopStorageNode(fake.node(), [&done]() { done = true; });

    LOGOS_ASSERT_FALSE(fake.destroyCalled);
    LOGOS_ASSERT_TRUE(done);
}

// A stop refused because another one is already in flight still gets its event,
// by then on a module the host has finished tearing down.
LOGOS_TEST(stop_ignores_the_event_landing_after_a_refused_stop) {
    FakeNode fake;
    fake.state = "running";
    fake.stopAccepted = false;
    int done = 0;

    stopStorageNode(fake.node(), [&done]() { ++done; });
    fake.stopped(true);

    LOGOS_ASSERT_FALSE(fake.destroyCalled);
    LOGOS_ASSERT_EQ(done, 1);
}
