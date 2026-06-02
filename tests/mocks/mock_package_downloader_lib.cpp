// Mock lgpd::PackageDownloaderLib for package_downloader_module unit tests
// (link-time substitution).
//
// Every method PackageDownloaderImpl calls on the library returns a JSON
// `std::string` (or, for the registry mutators, an error string), so —
// unlike package_manager_module, which needs struct registries — a plain
// LogosCMockStore string slot is enough. Each method records its call (so
// tests can assert it ran) and returns whatever the test configured via
// `t.mockCFunction("<key>").returns("<json-or-error>")`, falling back to a
// safe default when unset.
//
// Defaults when a test doesn't configure a return:
//   listRepositoriesJson / getCatalog*Json / resolveDependenciesJson → "[]"
//     (valid empty results — LogosList::parse won't throw)
//   refreshCatalogs / registry mutators → "" (empty == success)
//   downloadPackage → "" (empty path == download failure, per the impl's
//     pinnedDownload contract — a success test configures a path)

#include <logos_clib_mock.h>
#include <package_downloader_lib.h>   // resolves to tests/stubs/package_downloader_lib.h

#include <string>

namespace {

// Configured return for `key`, or `fallback` when the test left it unset.
// LOGOS_CMOCK_RETURN_STRING yields "" for an unset key; we can't tell that
// apart from an explicitly-configured "", which is fine here — every key
// whose fallback differs from "" (the JSON ones) is one a test sets only
// when it wants non-empty output.
std::string mockStr(const char* key, const char* fallback) {
    const char* r = LOGOS_CMOCK_RETURN_STRING(key);
    return (r && r[0]) ? std::string(r) : std::string(fallback);
}

} // namespace

namespace lgpd {

// ── PackageDownloaderLib ────────────────────────────────────────────────

PackageDownloaderLib::PackageDownloaderLib() {
    LOGOS_CMOCK_RECORD("PackageDownloaderLib_ctor");
}

PackageDownloaderLib::PackageDownloaderLib(std::string /*configPath*/) {
    LOGOS_CMOCK_RECORD("PackageDownloaderLib_ctor");
}

PackageDownloaderLib::~PackageDownloaderLib() {
    LOGOS_CMOCK_RECORD("PackageDownloaderLib_dtor");
}

RepositoryRegistry& PackageDownloaderLib::registry() { return registry_; }
const RepositoryRegistry& PackageDownloaderLib::registry() const { return registry_; }

std::string PackageDownloaderLib::listRepositoriesJson() {
    LOGOS_CMOCK_RECORD("listRepositoriesJson");
    return mockStr("listRepositoriesJson", "[]");
}

std::string PackageDownloaderLib::getCatalogJson() {
    LOGOS_CMOCK_RECORD("getCatalogJson");
    return mockStr("getCatalogJson", "[]");
}

std::string PackageDownloaderLib::getCatalogForRepoJson(const std::string& /*urlOrName*/) {
    LOGOS_CMOCK_RECORD("getCatalogForRepoJson");
    return mockStr("getCatalogForRepoJson", "[]");
}

std::string PackageDownloaderLib::refreshCatalogs() {
    LOGOS_CMOCK_RECORD("refreshCatalogs");
    return mockStr("refreshCatalogs", "");   // empty == success
}

std::string PackageDownloaderLib::downloadPackage(const std::string& /*repoUrlOrName*/,
                                                  const std::string& /*packageName*/,
                                                  const std::string& /*version*/,
                                                  const std::string& /*rootHash*/,
                                                  const std::string& /*outputDir*/) {
    LOGOS_CMOCK_RECORD("downloadPackage");
    return mockStr("downloadPackage", "");   // empty path == failure
}

std::string PackageDownloaderLib::resolveDependenciesJson(const std::string& /*dependenciesJson*/,
                                                          const std::string& /*installedPackagesJson*/) {
    LOGOS_CMOCK_RECORD("resolveDependenciesJson");
    return mockStr("resolveDependenciesJson", "[]");
}

// ── RepositoryRegistry ──────────────────────────────────────────────────

std::string RepositoryRegistry::addRepository(const std::string& /*url*/) {
    LOGOS_CMOCK_RECORD("addRepository");
    return mockStr("addRepository", "");   // empty == success
}

std::string RepositoryRegistry::removeRepository(const std::string& /*url*/) {
    LOGOS_CMOCK_RECORD("removeRepository");
    return mockStr("removeRepository", "");
}

std::string RepositoryRegistry::setEnabled(const std::string& /*url*/, bool /*enabled*/) {
    LOGOS_CMOCK_RECORD("setEnabled");
    return mockStr("setEnabled", "");
}

} // namespace lgpd
