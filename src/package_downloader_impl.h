#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <logos_json.h>
#include <logos_module_context.h>

namespace lgpd { class PackageDownloaderLib; }
class StorageFetcher;

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

    // Download source: the transports downloads may use. "any" (Logos
    // Storage, then HTTP), "logos" (Logos Storage only) or "http" (HTTP only),
    // persisted with the repositories. getCatalog() marks each version the
    // source cannot serve (`sourceAvailable: false`, `sourceUnavailableReason`),
    // and downloads never pick one.
    std::string getDownloadSource();
    LogosMap  setDownloadSource(const std::string& source);

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

    // Lifecycle. The module does nothing on load: a consumer calls start()
    // before its first call, and every other method answers "not started"
    // until then. start() loads the repository config and, when
    // storage_module is there, starts the storage node; it is idempotent.
    // stop() cancels the downloads in flight and lets go of the storage node,
    // without stopping it. getState() returns "stopped" or "running", and
    // stateChanged reports each change.
    LogosMap  start();
    LogosMap  stop();
    std::string getState();

    // catalogChanged fires on success from addRepository, removeRepository,
    // setRepositoryEnabled, and setDownloadSource when the source changes —
    // Subscribers re-fetch via listRepositories() / getCatalog().
    //
    // downloadProgress fires per package while its bytes are on the wire, in
    // install order, already rate-limited by the lib. `total` is 0 when
    // neither the transport nor the catalog knows the size — render that
    // indeterminate, never divide by it. Covers the TRANSFER ONLY:
    // verification and installation follow the last sample, so
    // received == total means "downloaded", not "done".
    //
    // downloadDone fires per package on success only. `source` is the URL
    // actually used: `logos:<network>:<cid>` or the https one.
    //
    // Requires concurrency:"multi" (metadata.json). A single-threaded module
    // holds the QtRO source thread for the whole download, and ModuleProxy
    // always QUEUES event emission onto it, so every sample would land in one
    // burst at the end. Don't revert that setting without removing this event.
    //
    // stateChanged fires when start() or stop() changes the state, with the
    // new one: "running" or "stopped", what getState() answers from then on.
    // A call that changes nothing (start() while running) emits nothing, and
    // neither does an unload.
logos_events:
    void catalogChanged();
    void downloadProgress(const std::string& packageName, uint64_t received, uint64_t total);
    void downloadDone(const std::string& packageName, const std::string& source);
    void stateChanged(const std::string& state);

protected:
    // Refuses new calls, stops events and fails the storage waits, then
    // waits for the calls in flight.
    LogosShutdown aboutToUnload() override;

private:
    // Counts a call that uses the lib, for aboutToUnload(). Defined in the .cpp:
    // the codegen reads this header line by line and would take its members
    // for module methods.
    class PendingLibCall;

    // What start() built: the lib, the storage fetcher and the storage watch.
    struct Run;

    // The current run, the call count and the unloading flag. Each call holds a
    // share, so one still running after the destructor frees nothing under it.
    struct CallState;

    void startStorage(const std::shared_ptr<Run>& run);

    std::shared_ptr<CallState> m_calls;

    // Serialises start() and stop(). Those run on workers ("multi"), and the
    // main thread never takes it: subscribing may wait for the main thread.
    std::mutex m_lifecycleMutex;
};
