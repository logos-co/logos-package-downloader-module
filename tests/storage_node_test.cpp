// The storage node start path, with the storage_module calls replaced by a
// recording fake.

#include <logos_test.h>
#include "storage_node.h"

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

    std::string initConfig;
    bool initCalled = false;
    bool startCalled = false;

    StorageNode node() {
        StorageNode n;

        n.isRunning = [this]() {
            return nodeRunning;
        };

        n.loadConfig = [this](std::string& error) {
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
            nodeRunning = nodeRunning || runningAfterStart;
            return startAccepted;
        };

        return n;
    }
};

} // namespace

LOGOS_TEST(start_inits_the_node_with_the_loaded_configuration) {
    FakeNode fake;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_EQ(fake.initConfig, fake.config);
    LOGOS_ASSERT_TRUE(fake.startCalled);
}

LOGOS_TEST(start_reports_a_configuration_that_could_not_be_loaded) {
    FakeNode fake;
    fake.configError = "Invalid configuration: expected a JSON object.";

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_EQ(error, fake.configError);
    LOGOS_ASSERT_FALSE(fake.initCalled);
}

// The node was created by another consumer between the two calls: its
// configuration is the one that counts, and the start still has to happen.
LOGOS_TEST(start_starts_the_node_another_consumer_created) {
    FakeNode fake;
    fake.initAccepted = false;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_TRUE(fake.startCalled);
}

LOGOS_TEST(start_leaves_a_running_node_alone) {
    FakeNode fake;
    fake.nodeRunning = true;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_FALSE(fake.initCalled);
    LOGOS_ASSERT_FALSE(fake.startCalled);
}

LOGOS_TEST(start_reports_a_refused_start) {
    FakeNode fake;
    fake.startAccepted = false;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_EQ(error, std::string("the storage module refused the start command"));
}

// The other consumer won the race between the check and the start: the node is
// up, so the refusal is not ours to report.
LOGOS_TEST(start_accepts_a_refusal_from_a_node_that_came_up_meanwhile) {
    FakeNode fake;
    fake.startAccepted = false;
    fake.runningAfterStart = true;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_TRUE(error.empty());
}
