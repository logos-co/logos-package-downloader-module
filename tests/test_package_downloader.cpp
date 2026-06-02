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

#include <string>

// ── Repository management ────────────────────────────────────────────────

LOGOS_TEST(addRepository_success_when_lib_returns_empty) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

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

    LogosMap r = impl.addRepository("nonsense");
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(), std::string("not a valid repo URL"));
}

LOGOS_TEST(removeRepository_forwards_and_succeeds) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

    LogosMap r = impl.removeRepository("https://example.com/logos-repo.json");
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("removeRepository"));
}

LOGOS_TEST(setRepositoryEnabled_forwards_and_succeeds) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

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

    LogosMap r = impl.refreshCatalog();
    LOGOS_ASSERT_TRUE(r["success"].get<bool>());
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("refreshCatalogs"));
}

LOGOS_TEST(refreshCatalog_failure_surfaces_error) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("refreshCatalogs").returns("repo X unreachable");
    PackageDownloaderImpl impl;

    LogosMap r = impl.refreshCatalog();
    LOGOS_ASSERT_FALSE(r["success"].get<bool>());
    LOGOS_ASSERT_EQ(r["error"].get<std::string>(), std::string("repo X unreachable"));
}

// ── Catalog ──────────────────────────────────────────────────────────────

LOGOS_TEST(getCatalog_parses_merged_json) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("getCatalogJson").returns(
        R"([{"name":"wallet_module","versions":[{"manifest":{"version":"1.0.0"}}]}])");
    PackageDownloaderImpl impl;

    LogosList catalog = impl.getCatalog();
    LOGOS_ASSERT_EQ(catalog.size(), static_cast<size_t>(1));
    LOGOS_ASSERT_EQ(catalog[0]["name"].get<std::string>(), std::string("wallet_module"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("getCatalogJson"));
}

LOGOS_TEST(getCatalog_empty_when_unset) {
    auto t = LogosTestContext("package_downloader");
    PackageDownloaderImpl impl;

    LogosList catalog = impl.getCatalog();   // mock default "[]"
    LOGOS_ASSERT_EQ(catalog.size(), static_cast<size_t>(0));
}

LOGOS_TEST(getCatalogForRepo_parses_scoped_json) {
    auto t = LogosTestContext("package_downloader");
    t.mockCFunction("getCatalogForRepoJson").returns(
        R"([{"name":"chat_module"}])");
    PackageDownloaderImpl impl;

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

    LogosMap r = impl.downloadPinned("my-catalog", "wallet_module", "1.0.0", "deadbeef");
    LOGOS_ASSERT_EQ(r["name"].get<std::string>(), std::string("wallet_module"));
    LOGOS_ASSERT_EQ(r["path"].get<std::string>(), std::string("/tmp/dl/wallet_module-1.0.0.lgx"));
    LOGOS_ASSERT_FALSE(r.contains("error"));
    LOGOS_ASSERT_EQ(r["version"].get<std::string>(), std::string("1.0.0"));
    LOGOS_ASSERT_EQ(r["rootHash"].get<std::string>(), std::string("deadbeef"));
    LOGOS_ASSERT_TRUE(t.cFunctionCalled("downloadPackage"));
}

LOGOS_TEST(downloadPinned_failure_returns_error_row) {
    auto t = LogosTestContext("package_downloader");
    // Unset downloadPackage → "" → empty path → failure.
    PackageDownloaderImpl impl;

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

    LogosList results = impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])");
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

    LogosList results = impl.downloadResolvedDependencies(R"([{"name":"chat_module"}])");
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

    LogosList results = impl.downloadResolvedDependencies(
        R"([{"name":"a"},{"name":"b"}])");
    LOGOS_ASSERT_EQ(results.size(), static_cast<size_t>(2));
    LOGOS_ASSERT_EQ(results[0]["name"].get<std::string>(), std::string("a"));
    LOGOS_ASSERT_EQ(results[1]["name"].get<std::string>(), std::string("b"));
    LOGOS_ASSERT_CONTAINS(results[0]["error"].get<std::string>(), std::string("downloader exception"));
}
