// Logos module bridge for the lgpd C++ library.
//
// Exposes the legacy single-repo/release-tag API to QML and other Logos
// modules while delegating to the new multi-repo `lgpd::PackageDownloaderLib`.
// The `releaseTag` parameters are accepted for source-compatibility with
// callers that haven't migrated yet but are otherwise ignored.

#include "package_downloader_impl.h"

#include <package_downloader_lib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Per-module data directory. There is currently no LogosAPI accessor for
// the module's data dir (see /repos/logos-cpp-sdk/) so we synthesise a
// sensible XDG-style default. The dir is created lazily on first write.
std::string defaultConfigPath() {
    fs::path base;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = fs::path(home) / ".config";
    } else {
        base = fs::temp_directory_path() / "logos";
    }
    return (base / "logos" / "package-downloader" / "repositories.json").string();
}

LogosMap makeResult(const std::string& err) {
    LogosMap r = LogosMap::object();
    r["success"] = err.empty();
    if (!err.empty()) r["error"] = err;
    return r;
}

// Single-shot download with full (repo, version, hash) pinning. Used by
// `downloadPackage` and `downloadPackages` below. Kept as a free function
// rather than a method so it can be reused without the codegen seeing
// overload ambiguity.
LogosMap pinnedDownload(lgpd::PackageDownloaderLib* lib,
                        const std::string& repoUrlOrName,
                        const std::string& packageName,
                        const std::string& version,
                        const std::string& rootHash) {
    LogosMap result = LogosMap::object();
    result["name"] = packageName;
    std::string path = lib->downloadPackage(repoUrlOrName, packageName, version, rootHash);
    if (path.empty()) {
        result["error"] = std::string("download failed for '") + packageName + "'";
    } else {
        result["path"] = path;
        if (!version.empty())  result["version"]  = version;
        if (!rootHash.empty()) result["rootHash"] = rootHash;
        if (!repoUrlOrName.empty()) result["repositoryUrl"] = repoUrlOrName;
    }
    return result;
}

} // namespace

PackageDownloaderImpl::PackageDownloaderImpl()
    : m_lib(new lgpd::PackageDownloaderLib(defaultConfigPath()))
{
    // Constructor seeds m_lib with an XDG-style fallback so callers that
    // bypass the LogosAPI framework (the `lgpd` CLI tests, unit tests
    // constructing the impl directly) still get a working downloader.
    // When the framework drives the module, onInit() below re-points
    // m_lib at the host-provided persistence directory. The replacement
    // is cheap because no fetches or registry mutations have happened
    // yet — the only sunk cost is reading the (possibly absent) XDG
    // config file once in the lib's constructor.
}

PackageDownloaderImpl::~PackageDownloaderImpl() { delete m_lib; }

void PackageDownloaderImpl::onContextReady() {
    // The codegen-generated provider has just populated the
    // LogosModuleContext base with the three host-injected paths
    // (modulePath / instanceId / instancePersistencePath). Re-anchor
    // m_lib at `<instancePersistencePath>/repositories.json` so the
    // repo config lives under the per-module data directory the host
    // owns the lifecycle of (e.g. Basecamp's
    // `module_data/package_downloader/<instanceId>/`).
    //
    // When loaded outside a host that provisions persistence (CLI
    // tests, unit tests using the impl directly) the getter returns
    // an empty string and we keep the XDG-default lib seeded by the
    // constructor. Safe to delete-and-replace here because the
    // framework guarantees onContextReady fires before any method
    // dispatch — no fetches or registry mutations have hit m_lib yet.
    if (instancePersistencePath().empty()) return;
    const std::string newPath =
        (fs::path(instancePersistencePath()) / "repositories.json").string();
    delete m_lib;
    m_lib = new lgpd::PackageDownloaderLib(newPath);
}

// ── Multi-repo API ─────────────────────────────────────────────────────────

LogosMap PackageDownloaderImpl::addRepository(const std::string& url) {
    return makeResult(m_lib->registry().addRepository(url));
}

LogosMap PackageDownloaderImpl::removeRepository(const std::string& url) {
    return makeResult(m_lib->registry().removeRepository(url));
}

LogosMap PackageDownloaderImpl::setRepositoryEnabled(const std::string& url, bool enabled) {
    return makeResult(m_lib->registry().setEnabled(url, enabled));
}

LogosList PackageDownloaderImpl::listRepositories() {
    return LogosList::parse(m_lib->listRepositoriesJson());
}

LogosMap PackageDownloaderImpl::refreshCatalog() {
    return makeResult(m_lib->refreshCatalogs());
}

LogosList PackageDownloaderImpl::getCatalog() {
    return LogosList::parse(m_lib->getCatalogJson());
}

LogosList PackageDownloaderImpl::getCatalogForRepo(const std::string& repoUrlOrName) {
    return LogosList::parse(m_lib->getCatalogForRepoJson(repoUrlOrName));
}

LogosMap PackageDownloaderImpl::downloadPinned(const std::string& repoUrlOrName,
                                                const std::string& packageName,
                                                const std::string& version,
                                                const std::string& rootHash) {
    return pinnedDownload(m_lib, repoUrlOrName, packageName, version, rootHash);
}

LogosList PackageDownloaderImpl::downloadResolvedDependencies(const std::string& dependenciesJson) {
    // Same exception fence as downloadPackages — see comment there.
    LogosList results = LogosList::array();
    try {
        LogosList resolved = LogosList::parse(m_lib->resolveDependenciesJson(dependenciesJson));
        for (const auto& entry : resolved) {
            if (!entry.is_object()) continue;
            if (entry.contains("error")) {
                LogosMap e = LogosMap::object();
                e["name"]  = entry.value("name", "");
                e["error"] = entry.value("error", "");
                results.push_back(e);
                break;
            }
            std::string name     = entry.value("name", "");
            std::string version  = entry.value("version", "");
            std::string rootHash = entry.value("rootHash", "");
            std::string repoUrl  = entry.value("repositoryUrl", "");
            results.push_back(pinnedDownload(m_lib, repoUrl, name, version, rootHash));
        }
    } catch (const std::exception& ex) {
        LogosMap e = LogosMap::object();
        e["error"] = std::string("downloader exception: ") + ex.what();
        results.push_back(e);
    } catch (...) {
        LogosMap e = LogosMap::object();
        e["error"] = std::string("downloader exception: unknown");
        results.push_back(e);
    }
    return results;
}
