// The storage node start path, with the storage_module calls replaced by a
// recording fake.

#include <logos_test.h>
#include "storage_node.h"

#include <string>

namespace {

struct FakeNode {
    std::string config = R"({"data-dir":"/tmp/storage"})";
    std::string configError;
    bool initAccepted = true;
    bool startAccepted = true;

    std::string initConfig;
    bool initCalled = false;
    bool startCalled = false;

    StorageNode node() {
        StorageNode n;

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

        return n;
    }
};

} // namespace

LOGOS_TEST(start_inits_the_node_with_the_migrated_configuration) {
    FakeNode fake;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_TRUE(error.empty());
    LOGOS_ASSERT_EQ(fake.initConfig, fake.config);
    LOGOS_ASSERT_TRUE(fake.startCalled);
}

LOGOS_TEST(start_reports_a_configuration_that_could_not_be_migrated) {
    FakeNode fake;
    fake.configError = "Invalid configuration: expected a JSON object.";

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_EQ(error, fake.configError);
    LOGOS_ASSERT_FALSE(fake.initCalled);
}

LOGOS_TEST(start_reports_a_refused_configuration) {
    FakeNode fake;
    fake.initAccepted = false;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_EQ(error, std::string("the storage module refused the configuration"));
    LOGOS_ASSERT_FALSE(fake.startCalled);
}

LOGOS_TEST(start_reports_a_refused_start) {
    FakeNode fake;
    fake.startAccepted = false;

    const std::string error = startStorageNode(fake.node());

    LOGOS_ASSERT_EQ(error, std::string("the storage module refused the start command"));
}
