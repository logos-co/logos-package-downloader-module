// The storage node start path, with the storage_module calls replaced by a
// recording fake.

#include <logos_test.h>
#include "storage_node.h"

#include <functional>
#include <string>

namespace {

struct FakeNode {
    bool nodeRunning = false;
    // The node another consumer brought up while this one was asking.
    bool runningAfterStart = false;
    std::string config = R"({"data-dir":"/tmp/storage"})";
    std::string configError;
    bool initAccepted = true;
    bool startAccepted = true;

    // The caller unloads from the start, or while init runs.
    bool unloading = false;
    bool unloadDuringInit = false;

    std::string initConfig;
    bool initCalled = false;
    bool startCalled = false;

    StorageNode node() {
        StorageNode n;

        n.isRunning = [this](std::function<void(bool)> done) {
            done(nodeRunning);
        };

        n.loadConfig = [this](std::function<void(const std::string&, const std::string&)> done) {
            done(config, configError);
        };

        n.init = [this](const std::string& cfg, std::function<void(bool)> done) {
            initCalled = true;
            initConfig = cfg;
            unloading = unloading || unloadDuringInit;
            done(initAccepted);
        };

        n.start = [this](std::function<void(bool)> done) {
            startCalled = true;
            nodeRunning = nodeRunning || runningAfterStart;
            done(startAccepted);
        };

        n.stopped = [this]() {
            return unloading;
        };

        return n;
    }

    // The fake answers at once, so the start is over when this returns.
    std::string start() {
        std::string error = "startStorageNode never called done";

        startStorageNode(node(), [&error](const std::string& result) {
            error = result;
        });

        return error;
    }
};

} // namespace

LOGOS_TEST(start_inits_the_node_with_the_loaded_configuration) {
    FakeNode fake;

    const std::string error = fake.start();

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_EQ(fake.initConfig, fake.config);
    LOGOS_ASSERT_TRUE(fake.startCalled);
}

LOGOS_TEST(start_reports_a_configuration_that_could_not_be_loaded) {
    FakeNode fake;
    fake.configError = "Invalid configuration: expected a JSON object.";

    const std::string error = fake.start();

    LOGOS_ASSERT_EQ(error, fake.configError);
    LOGOS_ASSERT_FALSE(fake.initCalled);
}

// The node was created by another consumer between the two calls: its
// configuration is the one that counts, and the start still has to happen.
LOGOS_TEST(start_starts_the_node_another_consumer_created) {
    FakeNode fake;
    fake.initAccepted = false;

    const std::string error = fake.start();

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_TRUE(fake.startCalled);
}

LOGOS_TEST(start_leaves_a_running_node_alone) {
    FakeNode fake;
    fake.nodeRunning = true;

    const std::string error = fake.start();

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_FALSE(fake.startCalled);
}

LOGOS_TEST(start_reports_a_refused_start) {
    FakeNode fake;
    fake.startAccepted = false;

    const std::string error = fake.start();

    LOGOS_ASSERT_EQ(error, std::string("the storage module refused the start command"));
}

// The other consumer won the race between the check and the start: the node is
// up, so the refusal is not ours to report.
LOGOS_TEST(start_accepts_a_refusal_from_a_node_that_came_up_meanwhile) {
    FakeNode fake;
    fake.startAccepted = false;
    fake.runningAfterStart = true;

    const std::string error = fake.start();

    LOGOS_ASSERT_TRUE(error.empty());
}

LOGOS_TEST(start_makes_no_call_once_the_module_unloads) {
    FakeNode fake;
    fake.unloading = true;

    const std::string error = fake.start();

    LOGOS_ASSERT_EQ(error, std::string("the module is unloading"));
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_FALSE(fake.startCalled);
}

LOGOS_TEST(start_stops_between_calls_when_the_module_unloads) {
    FakeNode fake;
    fake.unloadDuringInit = true;

    const std::string error = fake.start();

    LOGOS_ASSERT_EQ(error, std::string("the module is unloading"));
    LOGOS_ASSERT_TRUE(fake.initCalled);
    LOGOS_ASSERT_FALSE(fake.startCalled);
}
