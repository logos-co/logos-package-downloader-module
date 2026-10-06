// Unit tests for PackageDownloaderImpl — the bridge between the Logos
// module ABI and lgpd::PackageDownloaderLib.
//
// The library is replaced at link time by mocks/mock_package_downloader_lib.cpp
// (see tests/stubs/package_downloader_lib.h for the stubbed surface), so
// these tests exercise the bridge's own logic — JSON pass-through,
// success/error result shaping, the pinned-download mapping, and the
// resolve+download exception fence / error attribution — without any
// real network or disk access.
//
// Each test configures the mocked library's return for a method via
// `t.mockCFunction("<method>").returns("<json-or-error>")` and asserts on
// the LogosMap / LogosList the impl produces.

#include <logos_test.h>
#include "package_downloader_impl.h"
#include "mocks/mock_package_downloader_lib.h"
#include "mocks/mock_storage_fetcher_factory.h"

#include <functional>
#include <string>
#include <vector>

// ── Repository management ────────────────────────────────────────────────

LOGOS_TEST(addRepository_success_when_lib_returns_empty) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    // Unset return → mock yields "" → success.
    LogosMap r = impl.addRepository("https://example.com/logos-repo.json");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_FALSE(r.contains("error"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("addRepository"));
}

LOGOS_TEST(addRepository_failure_surfaces_error) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("addRepository").returns("not a valid repo URL");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.addRepository("nonsense");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(), std::string("not a valid repo URL"));
}

LOGOS_TEST(removeRepository_forwards_and_succeeds) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.removeRepository("https://example.com/logos-repo.json");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("removeRepository"));
}

LOGOS_TEST(setRepositoryEnabled_forwards_and_succeeds) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.setRepositoryEnabled("https://example.com/logos-repo.json", false);
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    // The impl calls registry().setEnabled(...) — the mock records "setEnabled".
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setEnabled"));
}

LOGOS_TEST(listRepositories_parses_json_array) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("listRepositoriesJson").returns(
        R"([{"url":"u1","enabled":true,"isDefault":true,"name":"default"},
            {"url":"u2","enabled":false,"isDefault":false,"name":"mine"}])");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList list = impl.listRepositories();
    LOGOS_ASSERT_EQ(list.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(list[0]["name"].get<std::string>(), std::string("default"));
    LOGOS_ASSERT_TRUE(list[0]["isDefault"].get<bool>());
    LOGOS_ASSERT_EQ(list[1]["name"].get<std::string>(), std::string("mine"));
    LOGOS_ASSERT_FALSE(list[1]["enabled"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("listRepositoriesJson"));
}

LOGOS_TEST(refreshCatalog_success_when_lib_returns_empty) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.refreshCatalog();
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("refreshCatalogs"));
}

LOGOS_TEST(refreshCatalog_failure_surfaces_error) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("refreshCatalogs").returns("repo X unreachable");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.refreshCatalog();
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(), std::string("repo X unreachable"));
}

// Basecamp and the Package Manager UI each keep a copy of the catalog; a
// refresh by one has to reach the other.
LOGOS_TEST(refreshCatalog_that_moved_the_catalog_emits_catalogChanged) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("catalogRevision").returns("1");
    PackageDownloaderImpl impl;
    impl.start();

    impl.refreshCatalog();
    LOGOS_ASSERT_TRUE(events.has("catalogChanged"));
}

LOGOS_TEST(refreshCatalog_that_fetched_the_same_catalog_emits_nothing) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    impl.refreshCatalog();
    LOGOS_ASSERT_FALSE(events.has("catalogChanged"));
}

// An index that failed earlier can be read by any call; the first call to
// see the new revision announces it, the rest stay quiet.
LOGOS_TEST(each_catalog_revision_is_announced_once) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("catalogRevision").returns("1");
    PackageDownloaderImpl impl;
    impl.start();

    impl.getCatalog();
    impl.resolveDependencies(R"(["wallet_module"])", "");
    impl.refreshCatalog();
    LOGOS_ASSERT_EQ(events.all("catalogChanged").size(), static_cast<size_t>(1));
}

// ── Catalog ──────────────────────────────────────────────────────────────

LOGOS_TEST(getCatalog_parses_merged_json) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("getCatalogJson").returns(
        R"([{"name":"wallet_module","versions":[{"manifest":{"version":"1.0.0"}}]}])");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList catalog = impl.getCatalog();
    LOGOS_ASSERT_EQ(catalog.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(catalog[0]["name"].get<std::string>(), std::string("wallet_module"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("getCatalogJson"));
}

LOGOS_TEST(getCatalog_empty_when_unset) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList catalog = impl.getCatalog();   // mock default "[]"
    LOGOS_ASSERT_EQ(catalog.size(), static_cast<size_t>(0));
}

LOGOS_TEST(getCatalogForRepo_parses_scoped_json) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("getCatalogForRepoJson").returns(
        R"([{"name":"chat_module"}])");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList catalog = impl.getCatalogForRepo("my-catalog");
    LOGOS_ASSERT_EQ(catalog.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(catalog[0]["name"].get<std::string>(), std::string("chat_module"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("getCatalogForRepoJson"));
}

// ── resolveDependencies (preview, no download) ───────────────────────────

LOGOS_TEST(resolveDependencies_passes_resolver_output_through) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns(
        R"([{"name":"dep_a","version":"1.0.0","rootHash":"h1","repositoryUrl":"r","url":"u","topLevel":false},
            {"name":"chat_module","version":"2.0.0","rootHash":"h2","repositoryUrl":"r","url":"u2","topLevel":true}])");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList plan = impl.resolveDependencies(R"([{"name":"chat_module"}])", "");
    LOGOS_ASSERT_EQ(plan.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(plan[0]["name"].get<std::string>(), std::string("dep_a"));
    LOGOS_ASSERT_FALSE(plan[0]["topLevel"].get<bool>());
    LOGOS_ASSERT_EQ(plan[1]["name"].get<std::string>(), std::string("chat_module"));
    LOGOS_ASSERT_TRUE(plan[1]["topLevel"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("resolveDependenciesJson"));
    // Pure preview — must NOT download.
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("downloadPackage"));
}

LOGOS_TEST(resolveDependencies_malformed_output_attributes_error_to_requested) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns("{ this is not valid json");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList plan = impl.resolveDependencies(R"([{"name":"chat_module"}])", "");
    LOGOS_ASSERT_EQ(plan.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(plan[0]["name"].get<std::string>(), std::string("chat_module"));
    LOGOS_ASSERT_CONTAINS(plan[0]["error"].get<std::string>(), std::string("resolver exception"));
}

// ── downloadPinned ───────────────────────────────────────────────────────

LOGOS_TEST(downloadPinned_success_returns_path) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");
    LOGOS_ASSERT_EQ(r["name"].get<std::string>(), std::string("wallet_module"));
    LOGOS_ASSERT_EQ(r["path"].get<std::string>(), std::string("/tmp/dl/wallet_module-1.0.0.lgx"));
    LOGOS_ASSERT_FALSE(r.contains("error"));
    LOGOS_ASSERT_EQ(r["version"].get<std::string>(), std::string("1.0.0"));
    LOGOS_ASSERT_EQ(r["rootHash"].get<std::string>(), std::string("deadbeef"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("downloadPackage"));
}

LOGOS_TEST(downloadPinned_reports_the_transport_that_served_the_package) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    t.mockCFunction("downloadSource").returns("logos:logos.test:zDvZRwzm3g3mPcYu1NmDKV5jCccw4FZ83XKyu85AjSCg7gH7zQdL");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");
    LOGOS_ASSERT_EQ(r["source"].get<std::string>(),
                    std::string("logos:logos.test:zDvZRwzm3g3mPcYu1NmDKV5jCccw4FZ83XKyu85AjSCg7gH7zQdL"));
}

LOGOS_TEST(downloadPinned_failure_returns_error_row) {
    auto t = LogosTestContext("package_downloader");
    // Unset downloadPackage → "" → empty path → failure.
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.downloadPinned("", "wallet_module", "", "");
    LOGOS_ASSERT_EQ(r["name"].get<std::string>(), std::string("wallet_module"));
    LOGOS_ASSERT_TRUE(r.contains("error"));
    LOGOS_ASSERT_FALSE(r.contains("path"));
}

// ── downloadResolvedDependencies (resolve + download) ────────────────────

LOGOS_TEST(downloadResolvedDependencies_downloads_each_resolved_entry) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns(
        R"([{"name":"chat_module","version":"2.0.0","rootHash":"h2","repositoryUrl":"r"}])");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/chat_module.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList results = impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])", "");
    LOGOS_ASSERT_EQ(results.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(results[0]["name"].get<std::string>(), std::string("chat_module"));
    LOGOS_ASSERT_EQ(results[0]["path"].get<std::string>(), std::string("/tmp/dl/chat_module.lgx"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("resolveDependenciesJson"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("downloadPackage"));
}

LOGOS_TEST(downloadResolvedDependencies_attributes_unnamed_resolver_error) {
    auto t = LogosTestContext("package_downloader");
    // Resolver reports an error with no `name`; the caller asked for
    // exactly one package, so the impl attributes the error to it.
    t.mockCFunction("resolveDependenciesJson").returns(
        R"([{"error":"no candidate matches 'chat_module'"}])");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList results = impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])", "");
    LOGOS_ASSERT_EQ(results.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(results[0]["name"].get<std::string>(), std::string("chat_module"));
    LOGOS_ASSERT_CONTAINS(results[0]["error"].get<std::string>(), std::string("no candidate"));
    // The error short-circuits before any download.
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("downloadPackage"));
}

LOGOS_TEST(downloadResolvedDependencies_malformed_output_attributes_error_per_request) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns("not json at all");
    PackageDownloaderImpl impl;
    impl.start();

    LogosList results = impl.downloadResolvedDependencies(
        R"([{"name":"a"},{"name":"b"}])", "");
    LOGOS_ASSERT_EQ(results.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(results[0]["name"].get<std::string>(), std::string("a"));
    LOGOS_ASSERT_EQ(results[1]["name"].get<std::string>(), std::string("b"));
    LOGOS_ASSERT_CONTAINS(results[0]["error"].get<std::string>(), std::string("downloader exception"));
}

// ── downloadProgress events ──────────────────────────────────────────────
//
// The mocked library replays a two-sample transfer (0/4096 then 4096/4096)
// for every downloadPackage call, standing in for what the real lib emits
// after its own rate limiting. These assert the bridge turns those samples
// into correctly-attributed `downloadProgress` events.

LOGOS_TEST(downloadPinned_emits_progress_events_for_the_package) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");

    auto progress = events.all("downloadProgress");
    LOGOS_ASSERT_EQ(progress.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(progress[0].data, std::string("wallet_module:0/4096"));
    LOGOS_ASSERT_EQ(progress[1].data, std::string("wallet_module:4096/4096"));
}

LOGOS_TEST(downloadPinned_emits_downloadDone_with_the_source) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    t.mockCFunction("downloadSource").returns("https://example.com/wallet_module-1.0.0.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");

    auto done = events.all("downloadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(done[0].data,
                    std::string("wallet_module:https://example.com/wallet_module-1.0.0.lgx"));
}

LOGOS_TEST(downloadPinned_emits_no_downloadDone_when_the_download_fails) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    // The real lib writes the source before checks that can still fail.
    t.mockCFunction("downloadSource").returns("https://example.com/wallet_module-1.0.0.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    impl.downloadPinned("", "wallet_module", "", "");

    LOGOS_ASSERT_FALSE(events.has("downloadDone"));
}

// A failed download still reports the bytes that did move — the UI needs
// them to keep the bar honest right up to the point of failure.
LOGOS_TEST(downloadPinned_emits_progress_even_when_the_download_fails) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    // Unset downloadPackage → "" → empty path → failure.
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.downloadPinned("", "wallet_module", "", "");
    LOGOS_ASSERT_TRUE(r.contains("error"));
    LOGOS_ASSERT_TRUE(events.has("downloadProgress"));
}

// Each package in a resolved plan reports under its OWN name, so the UI can
// attribute bytes to the right row while installing a dependency chain.
LOGOS_TEST(downloadResolvedDependencies_attributes_progress_per_package) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns(
        R"([{"name":"chat_module","version":"2.0.0","rootHash":"h2","repositoryUrl":"r"},)"
        R"({"name":"waku_module","version":"1.0.0","rootHash":"h1","repositoryUrl":"r"}])");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/pkg.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])", "");

    auto progress = events.all("downloadProgress");
    LOGOS_ASSERT_EQ(progress.size(), static_cast<size_t>(4));
    LOGOS_ASSERT_EQ(progress[0].data, std::string("chat_module:0/4096"));
    LOGOS_ASSERT_EQ(progress[1].data, std::string("chat_module:4096/4096"));
    LOGOS_ASSERT_EQ(progress[2].data, std::string("waku_module:0/4096"));
    LOGOS_ASSERT_EQ(progress[3].data, std::string("waku_module:4096/4096"));
}

LOGOS_TEST(downloadResolvedDependencies_emits_downloadDone_with_the_source) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns(
        R"([{"name":"chat_module","version":"2.0.0","rootHash":"h2","repositoryUrl":"r"},)"
        R"({"name":"waku_module","version":"1.0.0","rootHash":"h1","repositoryUrl":"r"}])");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/pkg.lgx");
    t.mockCFunction("downloadSource").returns("https://example.com/pkg.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])", "");

    auto done = events.all("downloadDone");
    LOGOS_ASSERT_EQ(done.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(done[0].data, std::string("chat_module:https://example.com/pkg.lgx"));
    LOGOS_ASSERT_EQ(done[1].data, std::string("waku_module:https://example.com/pkg.lgx"));
}

// A resolver error short-circuits before any transfer, so there is nothing
// to report — no phantom progress for a package that never downloaded.
LOGOS_TEST(downloadResolvedDependencies_emits_no_progress_when_resolution_fails) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("resolveDependenciesJson").returns(
        R"([{"error":"no candidate matches 'chat_module'"}])");
    PackageDownloaderImpl impl;
    impl.start();

    impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])", "");
    LOGOS_ASSERT_FALSE(events.has("downloadProgress"));
}

// ── Storage node bring-up ────────────────────────────────────────────────

namespace {

// Only an address: the mocked factory never dereferences it.
int unusedModules = 0;

void makeContextReady(PackageDownloaderImpl& impl) {
    impl._logosCoreSetLogosModulesPtr_(&unusedModules);
    impl._logosCoreSetContext_("", "", "");
}

// Answers at once, so the start is over when fireStorageReady() returns.
StorageNode fakeNode(bool& started) {
    StorageNode node;

    node.isRunning = [](std::function<void(bool)> done) { done(false); };
    node.loadConfig = [](std::function<void(const std::string&, const std::string&)> done) { done("{}", ""); };
    node.init = [](const std::string&, std::function<void(bool)> done) { done(true); };
    node.start = [&started](std::function<void(bool)> done) {
        started = true;
        done(true);
    };

    return node;
}

} // namespace

// Loading the module touches neither the config nor storage_module.
LOGOS_TEST(context_ready_starts_nothing) {
    auto t = LogosTestContext("package_downloader");
    fireStorageReady = nullptr;
    PackageDownloaderImpl impl;

    makeContextReady(impl);

    LOGOS_ASSERT_FALSE(t.cFunctionCalled("PackageDownloaderLib_ctor"));
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("setStorageFetcher"));
    LOGOS_ASSERT_FALSE(static_cast<bool>(fireStorageReady));
    LOGOS_ASSERT_EQ(impl.getState(), std::string("stopped"));
}

// Before any call is served: setStorageFetcher then never waits on the lib's
// lock, which a first catalog fetch holds across the network.
LOGOS_TEST(start_installs_the_storage_fetcher) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

    makeContextReady(impl);
    impl.start();

    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setStorageFetcher"));
}

LOGOS_TEST(storage_ready_after_stop_starts_nothing) {
    auto t = LogosTestContext("package_downloader");
    bool started = false;
    const StorageNode defaultNode = fakeStorageNode;
    fakeStorageNode = fakeNode(started);
    PackageDownloaderImpl impl;
    makeContextReady(impl);
    impl.start();
    impl.stop();

    fireStorageReady();

    fakeStorageNode = defaultNode;

    LOGOS_ASSERT_FALSE(started);
}

LOGOS_TEST(storage_ready_starts_the_node) {
    auto t = LogosTestContext("package_downloader");
    bool started = false;
    const StorageNode defaultNode = fakeStorageNode;
    fakeStorageNode = fakeNode(started);
    PackageDownloaderImpl impl;
    makeContextReady(impl);
    impl.start();

    fireStorageReady();

    // Before the assertion, which may end the test: the fake holds a
    // reference to `started`.
    fakeStorageNode = defaultNode;

    LOGOS_ASSERT_TRUE(started);
}

// Nothing to subscribe to before the module is there: a host without it
// keeps no pending subscription.
LOGOS_TEST(storage_events_are_subscribed_on_the_first_ready) {
    auto t = LogosTestContext("package_downloader");
    fakeFetcherSubscriptions = 0;
    PackageDownloaderImpl impl;
    makeContextReady(impl);
    impl.start();

    const int beforeReady = fakeFetcherSubscriptions;
    fireStorageReady();
    fireStorageReady();

    LOGOS_ASSERT_EQ(beforeReady, 0);
    LOGOS_ASSERT_EQ(fakeFetcherSubscriptions, 3);
}

// ── Lifecycle ────────────────────────────────────────────────────────────

LOGOS_TEST(a_call_before_start_is_refused) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

    LogosMap r = impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");

    LOGOS_ASSERT_CONTAINS(r.value("error", ""), std::string("not started"));
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("downloadPackage"));
    LOGOS_ASSERT_FALSE(impl.addRepository("https://example.com/r.json")["success"].get<bool>());
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("addRepository"));
}

LOGOS_TEST(start_runs_the_downloader_once) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

    LOGOS_ASSERT_EQ(impl.getState(), std::string("stopped"));
    LOGOS_ASSERT_TRUE(impl.start()["success"].get<bool>());
    LOGOS_ASSERT_TRUE(impl.start()["success"].get<bool>());

    LOGOS_ASSERT_EQ(impl.getState(), std::string("running"));
    LOGOS_ASSERT_EQ(t.cFunctionCallCount("PackageDownloaderLib_ctor"), 1);
}

LOGOS_TEST(stop_refuses_later_calls_and_start_serves_them_again) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LOGOS_ASSERT_TRUE(impl.stop()["success"].get<bool>());
    LOGOS_ASSERT_EQ(impl.getState(), std::string("stopped"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("PackageDownloaderLib_dtor"));
    LOGOS_ASSERT_FALSE(impl.addRepository("https://example.com/r.json")["success"].get<bool>());
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("addRepository"));

    impl.start();

    LOGOS_ASSERT_EQ(impl.getState(), std::string("running"));
    LOGOS_ASSERT_TRUE(impl.addRepository("https://example.com/r.json")["success"].get<bool>());
}

LOGOS_TEST(stop_cancels_a_download_in_flight) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    duringDownloadPackage = [&]() { impl.stop(); };

    const LogosMap result =
        impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");
    duringDownloadPackage = nullptr;

    LOGOS_ASSERT_FALSE(result.contains("path"));
    LOGOS_ASSERT_CONTAINS(result.value("error", ""), std::string("download cancelled"));
    LOGOS_ASSERT_EQ(impl.getState(), std::string("stopped"));
}

LOGOS_TEST(start_and_stop_report_each_state_change) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

    impl.start();
    impl.start();   // already running: nothing changes
    impl.stop();
    impl.stop();    // already stopped: nothing changes
    impl.start();

    std::vector<std::string> states;
    for (const auto& e : events.all("stateChanged")) states.push_back(e.data);

    LOGOS_ASSERT_EQ(states.size(), static_cast<size_t>(3));
    LOGOS_ASSERT_EQ(states[0], std::string("running"));
    LOGOS_ASSERT_EQ(states[1], std::string("stopped"));
    LOGOS_ASSERT_EQ(states[2], std::string("running"));
}

// The host tears the event path down: an unload reports nothing.
LOGOS_TEST(unload_reports_no_state_change) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    impl._logosCoreAboutToUnload_();
    impl.stop();
    impl.start();

    LOGOS_ASSERT_EQ(events.all("stateChanged").size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(impl.getState(), std::string("stopped"));
}

LOGOS_TEST(start_after_aboutToUnload_is_refused) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl._logosCoreAboutToUnload_();

    LogosMap r = impl.start();

    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r.value("error", ""), std::string("the module is unloading"));
    LOGOS_ASSERT_EQ(impl.getState(), std::string("stopped"));
}

// ── Unload ───────────────────────────────────────────────────────────────

LOGOS_TEST(a_call_after_aboutToUnload_is_refused) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LOGOS_ASSERT_TRUE(impl._logosCoreAboutToUnload_() == LogosShutdown::Synchronous);

    LogosMap r = impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");

    LOGOS_ASSERT_EQ(r.value("error", ""), std::string("the module is unloading"));
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("downloadPackage"));
}

// Unload reaches the library's cancellation callback during a download, so
// it stops the transfer and the host sees no events or successful path.
LOGOS_TEST(a_download_in_flight_is_cancelled_after_aboutToUnload) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    PackageDownloaderImpl impl;
    impl.start();

    int finished = 0;
    impl._logosCoreSetUnloadFinished_([&finished]() { ++finished; });

    LogosShutdown shutdown = LogosShutdown::Synchronous;
    duringDownloadPackage = [&]() { shutdown = impl._logosCoreAboutToUnload_(); };

    const LogosMap result =
        impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");
    duringDownloadPackage = nullptr;

    LOGOS_ASSERT_TRUE(shutdown == LogosShutdown::Asynchronous);
    LOGOS_ASSERT_FALSE(result.contains("path"));
    LOGOS_ASSERT_CONTAINS(result.value("error", ""), std::string("download cancelled"));
    LOGOS_ASSERT_FALSE(events.has("downloadProgress"));
    LOGOS_ASSERT_FALSE(events.has("downloadDone"));
    LOGOS_ASSERT_EQ(finished, 1);
}

// The host destroys the impl at process exit, possibly under a call that
// outlived its wait: the call's share keeps the lib alive until it returns.
LOGOS_TEST(a_call_in_flight_keeps_the_lib_past_the_destructor) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("downloadPackage").returns("/tmp/dl/wallet_module-1.0.0.lgx");
    auto* impl = new PackageDownloaderImpl();
    impl->start();

    bool freedUnderTheCall = true;
    duringDownloadPackage = [&]() {
        impl->_logosCoreAboutToUnload_();
        delete impl;
        freedUnderTheCall = t.cFunctionCalled("PackageDownloaderLib_dtor");
    };

    impl->downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");
    duringDownloadPackage = nullptr;

    LOGOS_ASSERT_FALSE(freedUnderTheCall);
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("PackageDownloaderLib_dtor"));
}

// ── Download source ──────────────────────────────────────────────────────

LOGOS_TEST(setDownloadSource_persists_the_source_and_emits_catalogChanged) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.setDownloadSource("logos");

    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("setDownloadSource"));
    LOGOS_ASSERT_EQ(impl.getDownloadSource(), std::string("logos"));
    LOGOS_ASSERT_TRUE(events.has("catalogChanged"));
}

LOGOS_TEST(setDownloadSource_refuses_an_unknown_source) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.setDownloadSource("ftp");

    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_CONTAINS(r["error"].get<std::string>(), std::string("any, logos or http"));
    LOGOS_ASSERT_FALSE(t.cFunctionCalled("setDownloadSource"));
    LOGOS_ASSERT_EQ(impl.getDownloadSource(), std::string("any"));
    LOGOS_ASSERT_FALSE(events.has("catalogChanged"));
}

// Nothing to refresh: the catalog's availability marks did not move.
LOGOS_TEST(setDownloadSource_to_the_current_source_emits_nothing) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.setDownloadSource("any");

    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_FALSE(events.has("catalogChanged"));
}

LOGOS_TEST(setDownloadSource_surfaces_a_save_error) {
    logos_test::EventCapture events;
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("setDownloadSource").returns("cannot write config file: /x");
    PackageDownloaderImpl impl;
    impl.start();

    LogosMap r = impl.setDownloadSource("http");

    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(), std::string("cannot write config file: /x"));
    LOGOS_ASSERT_FALSE(events.has("catalogChanged"));
}
