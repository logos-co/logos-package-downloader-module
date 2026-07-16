#pragma once

#include <string>
#include <vector>
#include <logos_json.h>
#include <logos_module_context.h>

namespace lgpd { class PackageDownloaderLib; }

/**
 * Bridges the lgpd C++ library to the Logos module ABI.
 *
 * Surface is the multi-repo API. The legacy single-repo / release-tag
 * shims (`getReleases`, `getPackages(tag[, category])`, `getCategories(tag)`,
 * `downloadPackage(tag, name)`, `downloadPackages(tag, names)`,
 * `resolveDependencies(tag, names)`) were removed once the QML UI migrated
 * to `getCatalog` / `downloadResolvedDependencies` / `downloadPinned`. The
 * catalog is the union across every enabled repository (the hardcoded
 * default plus any user repositories configured via the `lgpd` CLI's
 * `repo` commands or this module's `addRepository`).
 */
class PackageDownloaderImpl : public LogosModuleContext {
public:
    PackageDownloaderImpl();
    ~PackageDownloaderImpl();

    PackageDownloaderImpl(const PackageDownloaderImpl&) = delete;
    PackageDownloaderImpl& operator=(const PackageDownloaderImpl&) = delete;

    // NB: every method declaration here MUST be on a single line. The
    // Logos C++ codegen's `--from-header` parser scans line-by-line and
    // silently drops methods whose declaration wraps. See
    // repos/logos-cpp-sdk/cpp-generator/experimental/impl_header_parser.cpp,
    // around `if (line.endsWith(';'))`.

    // Multi-repo API — used by the "Manage Repositories" UI and by any
    // caller that needs per-repo, per-version, per-signer downloads.
    // All mutating calls return `{ "success": bool, "error": string? }`.
    LogosMap  addRepository(const std::string& url);
    LogosMap  removeRepository(const std::string& url);
    LogosMap  setRepositoryEnabled(const std::string& url, bool enabled);
    LogosList listRepositories();
    LogosMap  refreshCatalog();
    LogosList getCatalog();
    LogosList getCatalogForRepo(const std::string& repoUrlOrName);

    // Pinned download — picks an exact (repository, version, rootHash)
    // candidate from the merged catalog. Empty args mean "any matching".
    LogosMap  downloadPinned(const std::string& repoUrlOrName, const std::string& packageName, const std::string& version, const std::string& rootHash);

    // Resolve a manifest-style dependency list and download every resolved
    // package in install order. The JSON shape is what `manifest.dependencies`
    // produces (string or {name,version?,signer?}). installedPackagesJson is
    // the same optional [{name,version,rootHash}] shape resolveDependencies
    // takes: when supplied, an already-installed dep whose version satisfies the
    // range is kept rather than re-downloaded at the newest version. Pass "" to
    // resolve every transitive from the catalog. Must match what the preview
    // (resolveDependencies) was given, or the download silently upgrades a dep
    // the preview said would stay put.
    LogosList downloadResolvedDependencies(const std::string& dependenciesJson, const std::string& installedPackagesJson);

    // Same resolver pass as downloadResolvedDependencies but no
    // download. Callers preview the dep impact, then drive the actual
    // install through the regular download path once the user
    // confirms. installedPackagesJson is optional shape
    // [name version rootHash] entries; when supplied the resolver
    // short-circuits transitive deps already satisfied on disk. Pass
    // empty string to disable that and get every transitive resolved
    // from the catalog.
    LogosList resolveDependencies(const std::string& dependenciesJson, const std::string& installedPackagesJson);

    // catalogChanged fires on success from addRepository, removeRepository,
    // and setRepositoryEnabled — Subscribers re-fetch via
    // listRepositories() / getCatalog().
logos_events:
    void catalogChanged();

protected:
    // Fires once, after the framework has populated the LogosModuleContext
    // getters (`modulePath()`, `instanceId()`, `instancePersistencePath()`)
    // and before any method is dispatched. We use it to re-anchor m_lib's
    // config file under the host-provided persistence directory; the
    // constructor seeds an XDG fallback so callers bypassing the
    // framework (lgpd CLI, unit tests) still see a working lib.
    void onContextReady() override;

private:
    lgpd::PackageDownloaderLib* m_lib;
};
