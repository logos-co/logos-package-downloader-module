// Logos module bridge for the lgpd C++ library.
//
// Exposes the multi-repo catalog API of `lgpd::PackageDownloaderLib` to
// QML and other Logos modules over IPC: repository management, catalog
// reads, pinned + dependency-resolving downloads, and a download-free
// `resolveDependencies` preview. The legacy single-repo/release-tag
// shims have been removed.

#include "package_downloader_impl.h"
#include "storage_fetcher.h"
#include "storage_fetcher_factory.h"

#include <package_downloader_lib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>   // std::move, std::exchange
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
        // No XDG_CONFIG_HOME and no HOME: fall back to a temp dir. This
        // already ends in a `logos` segment, so don't append another one
        // below — otherwise the path became `<tmp>/logos/logos/…`,
        // inconsistent with the XDG/HOME branches which add exactly one
        // `logos` segment.
        return (fs::temp_directory_path() / "logos" / "package-downloader"
                / "repositories.json").string();
    }
    return (base / "logos" / "package-downloader" / "repositories.json").string();
}

LogosMap makeResult(const std::string& err) {
    LogosMap r = LogosMap::object();
    r["success"] = err.empty();
    if (!err.empty()) r["error"] = err;
    return r;
}

// The answer to a call that arrives after aboutToUnload().
const char* const kUnloading = "the module is unloading";

// Where a download's byte counts go. Empty means "report nothing", and the
// lib then skips curl's progress machinery entirely.
using ProgressSink = std::function<void(const std::string& packageName,
                                        std::uint64_t received,
                                        std::uint64_t total)>;

// Single-shot download with full (repo, version, hash) pinning. Used by
// `downloadPinned` and `downloadResolvedDependencies` below. Kept as a
// free function rather than a method so it can be reused without the
// codegen seeing overload ambiguity.
LogosMap pinnedDownload(lgpd::PackageDownloaderLib* lib,
                        const std::string& repoUrlOrName,
                        const std::string& packageName,
                        const std::string& version,
                        const std::string& rootHash,
                        const ProgressSink& onProgress = {}) {
    LogosMap result = LogosMap::object();
    result["name"] = packageName;

    lgpd::ProgressFn progressFn;
    if (onProgress) {
        progressFn = [&](std::uint64_t received, std::uint64_t total) {
            onProgress(packageName, received, total);
        };
    }

    std::string err;
    std::string source;
    std::string path = lib->downloadPackage(repoUrlOrName, packageName, err,
                                            version, rootHash, "", progressFn, &source);

    if (!source.empty()) {
        result["source"] = source;
    }

    if (path.empty()) {
        std::string msg = std::string("download failed for '") + packageName + "'";

        if (!err.empty()) {
            msg += " — " + err;
        }

        result["error"] = msg;
    } else {
        result["path"] = path;
        if (!version.empty())  result["version"]  = version;
        if (!rootHash.empty()) result["rootHash"] = rootHash;
        if (!repoUrlOrName.empty()) result["repositoryUrl"] = repoUrlOrName;
    }
    return result;
}

} // namespace

struct PackageDownloaderImpl::CallState {
    std::mutex mutex;
    std::shared_ptr<lgpd::PackageDownloaderLib> lib;
    int pending = 0;

    // Set by aboutToUnload(): new calls are refused and no event is emitted.
    std::atomic<bool> unloading{false};

    // Set when aboutToUnload() returned Asynchronous; the destructor clears it.
    std::function<void()> onDrained;
};

class PackageDownloaderImpl::PendingLibCall {
public:
    explicit PendingLibCall(PackageDownloaderImpl& impl);
    ~PendingLibCall();

    // False for a call that came after aboutToUnload(): it must not start.
    explicit operator bool() const { return m_lib != nullptr; }

    lgpd::PackageDownloaderLib* lib() const { return m_lib.get(); }

    // Once true, the call emits nothing: the host tears the event path down.
    bool unloading() const { return m_state->unloading; }

private:
    std::shared_ptr<CallState> m_state;
    std::shared_ptr<lgpd::PackageDownloaderLib> m_lib;
};

PackageDownloaderImpl::PackageDownloaderImpl()
    : m_calls(std::make_shared<CallState>())
{
    // Constructor seeds the lib with an XDG-style fallback so callers that
    // bypass the LogosAPI framework (the `lgpd` CLI tests, unit tests
    // constructing the impl directly) still get a working downloader.
    // When the framework drives the module, onContextReady() below
    // re-points it at the host-provided persistence directory. The
    // replacement is cheap because no fetches or registry mutations have
    // happened yet — the only sunk cost is reading the (possibly absent)
    // XDG config file once in the lib's constructor.
    m_calls->lib = std::make_shared<lgpd::PackageDownloaderLib>(defaultConfigPath());
}

// The host runs this at process exit, after its grace period: a call may still
// be running, and keeps the lib alive through its own share.
PackageDownloaderImpl::~PackageDownloaderImpl() {
    if (m_cancelWatchSubscription) {
        m_cancelWatchSubscription();
    }

    std::lock_guard<std::mutex> lock(m_calls->mutex);

    m_calls->onDrained = nullptr;
    m_calls->lib.reset();
}

void PackageDownloaderImpl::onContextReady() {
    // The codegen-generated provider has just populated the
    // LogosModuleContext base with the three host-injected paths
    // (modulePath / instanceId / instancePersistencePath). Re-anchor
    // the lib at `<instancePersistencePath>/repositories.json` so the
    // repo config lives under the per-module data directory the host
    // owns the lifecycle of (e.g. Basecamp's
    // `module_data/package_downloader/<instanceId>/`).
    //
    // When loaded outside a host that provisions persistence (CLI
    // tests, unit tests using the impl directly) the getter returns
    // an empty string and we keep the XDG-default lib seeded by the
    // constructor. Safe to replace here because the framework
    // guarantees onContextReady fires before any method dispatch —
    // no fetches or registry mutations have hit the lib yet.

    std::shared_ptr<lgpd::PackageDownloaderLib> lib;

    if (!instancePersistencePath().empty()) {
        const std::string newPath =
            (fs::path(instancePersistencePath()) / "repositories.json").string();
        lib = std::make_shared<lgpd::PackageDownloaderLib>(newPath);
    }

    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        if (lib) {
            m_calls->lib = lib;
        }

        lib = m_calls->lib;
    }

    m_storageFetcher = makeStorageFetcher(modules());
    lib->setStorageFetcher(m_storageFetcher);

    m_cancelWatchSubscription = watchStorageReady(modules(), [this]() {
        startStorage();
    });
}

void PackageDownloaderImpl::startStorage() {
    // Kept until the last reply: aboutToUnload() waits for it.
    auto call = std::make_shared<PendingLibCall>(*this);

    if (!*call) {
        return;
    }

    if (m_storageFetcher) {
        m_storageFetcher->subscribe();
    }

    StorageNode node = makeStorageNode(modules());
    node.stopped = [call]() { return call->unloading(); };

    startStorageNode(node, [call](const std::string& error) {
        if (!error.empty()) {
            fprintf(stderr, "PackageDownloaderImpl::startStorage: %s\n", error.c_str());
        }
    });
}

LogosShutdown PackageDownloaderImpl::aboutToUnload() {
    if (m_cancelWatchSubscription) {
        m_cancelWatchSubscription();
        m_cancelWatchSubscription = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        m_calls->unloading = true;
    }

    if (m_storageFetcher) {
        m_storageFetcher->cancelPendingDownloads();
    }

    std::lock_guard<std::mutex> lock(m_calls->mutex);

    if (m_calls->pending == 0) {
        return LogosShutdown::Synchronous;
    }

    m_calls->onDrained = [this]() { unloadFinished(); };

    return LogosShutdown::Asynchronous;
}

PackageDownloaderImpl::PendingLibCall::PendingLibCall(PackageDownloaderImpl& impl)
    : m_state(impl.m_calls)
{
    std::lock_guard<std::mutex> lock(m_state->mutex);

    if (m_state->unloading) {
        return;
    }

    ++m_state->pending;
    m_lib = m_state->lib;
}

PackageDownloaderImpl::PendingLibCall::~PendingLibCall() {
    if (!m_lib) {
        return;
    }

    std::function<void()> onDrained;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);

        if (--m_state->pending == 0) {
            onDrained = std::exchange(m_state->onDrained, nullptr);
        }
    }

    if (onDrained) {
        onDrained();
    }
}

// ── Multi-repo API ─────────────────────────────────────────────────────────

LogosMap PackageDownloaderImpl::addRepository(const std::string& url) {
    PendingLibCall call(*this);
    if (!call) return makeResult(kUnloading);
    const std::string err = call.lib()->registry().addRepository(url);
    if (err.empty() && !call.unloading()) catalogChanged();
    return makeResult(err);
}

LogosMap PackageDownloaderImpl::removeRepository(const std::string& url) {
    PendingLibCall call(*this);
    if (!call) return makeResult(kUnloading);
    const std::string err = call.lib()->registry().removeRepository(url);
    if (err.empty() && !call.unloading()) catalogChanged();
    return makeResult(err);
}

LogosMap PackageDownloaderImpl::setRepositoryEnabled(const std::string& url, bool enabled) {
    PendingLibCall call(*this);
    if (!call) return makeResult(kUnloading);
    const std::string err = call.lib()->registry().setEnabled(url, enabled);
    if (err.empty() && !call.unloading()) catalogChanged();
    return makeResult(err);
}

LogosList PackageDownloaderImpl::listRepositories() {
    PendingLibCall call(*this);
    if (!call) return LogosList::array();
    return LogosList::parse(call.lib()->listRepositoriesJson());
}

LogosMap PackageDownloaderImpl::refreshCatalog() {
    PendingLibCall call(*this);
    if (!call) return makeResult(kUnloading);
    return makeResult(call.lib()->refreshCatalogs());
}

LogosList PackageDownloaderImpl::getCatalog() {
    PendingLibCall call(*this);
    if (!call) return LogosList::array();
    return LogosList::parse(call.lib()->getCatalogJson());
}

LogosList PackageDownloaderImpl::getCatalogForRepo(const std::string& repoUrlOrName) {
    PendingLibCall call(*this);
    if (!call) return LogosList::array();
    return LogosList::parse(call.lib()->getCatalogForRepoJson(repoUrlOrName));
}

std::string PackageDownloaderImpl::getDownloadSource() {
    PendingLibCall call(*this);
    if (!call) return {};
    return lgpd::downloadSourceName(call.lib()->registry().downloadSource());
}

LogosMap PackageDownloaderImpl::setDownloadSource(const std::string& source) {
    PendingLibCall call(*this);
    if (!call) return makeResult(kUnloading);

    const auto parsed = lgpd::parseDownloadSource(source);
    if (!parsed) {
        return makeResult("unknown download source '" + source + "': expected any, logos or http");
    }

    lgpd::RepositoryRegistry& registry = call.lib()->registry();
    const bool changed = registry.downloadSource() != *parsed;
    const std::string err = registry.setDownloadSource(*parsed);

    // The catalog's availability marks follow the source.
    if (err.empty() && changed && !call.unloading()) catalogChanged();
    return makeResult(err);
}

LogosMap PackageDownloaderImpl::downloadPinned(const std::string& repoUrlOrName,
                                                const std::string& packageName,
                                                const std::string& version,
                                                const std::string& rootHash) {
    PendingLibCall call(*this);

    if (!call) {
        LogosMap refused = LogosMap::object();
        refused["name"] = packageName;
        refused["error"] = kUnloading;
        return refused;
    }

    LogosMap result = pinnedDownload(call.lib(), repoUrlOrName, packageName, version, rootHash,
                                     [this, &call](const std::string& name, std::uint64_t received,
                                                   std::uint64_t total) {
                                         if (!call.unloading()) {
                                             downloadProgress(name, received, total);
                                         }
                                     });

    // A successful download contains the path where the package was downloaded.
    if (result.contains("path") && !call.unloading()) {
        downloadDone(packageName, result.value("source", ""));
    }

    return result;
}

LogosList PackageDownloaderImpl::downloadResolvedDependencies(const std::string& dependenciesJson, const std::string& installedPackagesJson) {
    PendingLibCall call(*this);
    // Exception fence: the resolver/downloader can throw on malformed
    // catalog data; we convert any throw into per-package error rows
    // (below) so one bad entry never takes down the whole batch.
    // `resolveDependencies` reuses this same pattern.
    LogosList results = LogosList::array();

    // Extract the requested top-level names up front so a failure that
    // throws *before* resolveDependenciesJson emits any per-entry output
    // can still be attributed to the package(s) the caller asked for.
    // Without this the catch-all below produces a nameless `{ "error" }`
    // row; the UI keys install/Failed badges by package name, so a
    // nameless row silently no-ops the model update and the row reverts
    // to "Not Installed" with no error surfaced (observed with
    // wallet_module against an older index that trips a type_error in
    // the resolver). The manifest dependency shape is either
    // `["name", ...]` or `[{ "name": "...", ... }, ...]`; anything else
    // is tolerated and simply yields no names (falls back to the
    // historical nameless error row).
    std::vector<std::string> requestedNames;
    try {
        LogosList deps = LogosList::parse(dependenciesJson);
        if (deps.is_array()) {
            for (const auto& d : deps) {
                if (d.is_string()) {
                    requestedNames.push_back(d.get<std::string>());
                } else if (d.is_object()) {
                    std::string n = d.value("name", "");
                    if (!n.empty()) requestedNames.push_back(std::move(n));
                }
            }
        }
    } catch (...) {
        // Best-effort attribution only; leave requestedNames empty.
    }

    auto pushError = [&](const std::string& msg) {
        if (requestedNames.empty()) {
            LogosMap e = LogosMap::object();
            e["error"] = msg;
            results.push_back(e);
            return;
        }
        // One error row per requested package so every UI row that was
        // marked "Installing" gets a matching Failed update by name.
        for (const auto& n : requestedNames) {
            LogosMap e = LogosMap::object();
            e["name"]  = n;
            e["error"] = msg;
            results.push_back(e);
        }
    };

    if (!call) {
        pushError(kUnloading);
        return results;
    }

    try {
        LogosList resolved = LogosList::parse(call.lib()->resolveDependenciesJson(dependenciesJson, installedPackagesJson));
        for (const auto& entry : resolved) {
            if (!entry.is_object()) continue;
            if (entry.contains("error")) {
                LogosMap e = LogosMap::object();
                // Prefer the resolver's own name; if it didn't attribute
                // the failure and the caller asked for exactly one
                // package, attribute it to that so the UI can react.
                std::string errName = entry.value("name", "");
                if (errName.empty() && requestedNames.size() == 1)
                    errName = requestedNames.front();
                e["name"]  = errName;
                e["error"] = entry.value("error", "");
                results.push_back(e);
                break;
            }
            std::string name     = entry.value("name", "");
            std::string version  = entry.value("version", "");
            std::string rootHash = entry.value("rootHash", "");
            std::string repoUrl  = entry.value("repositoryUrl", "");

            // Unloading: the host stops waiting soon, so start no further package.
            if (call.unloading()) {
                LogosMap e = LogosMap::object();
                e["name"]  = name;
                e["error"] = kUnloading;
                results.push_back(e);
                break;
            }

            LogosMap downloaded = pinnedDownload(
                call.lib(), repoUrl, name, version, rootHash,
                [this, &call](const std::string& pkg, std::uint64_t received,
                              std::uint64_t total) {
                    if (!call.unloading()) {
                        downloadProgress(pkg, received, total);
                    }
                });

            // A successful download contains the path where the package was downloaded.
            if (downloaded.contains("path") && !call.unloading()) {
                downloadDone(name, downloaded.value("source", ""));
            }

            results.push_back(std::move(downloaded));
        }
    } catch (const std::exception& ex) {
        pushError(std::string("downloader exception: ") + ex.what());
    } catch (...) {
        pushError("downloader exception: unknown");
    }
    return results;
}

LogosList PackageDownloaderImpl::resolveDependencies(const std::string& dependenciesJson,
                                                    const std::string& installedPackagesJson) {
    PendingLibCall call(*this);
    // Same exception fence + per-input attribution as
    // downloadResolvedDependencies — see comments there.
    LogosList results = LogosList::array();

    std::vector<std::string> requestedNames;
    try {
        LogosList deps = LogosList::parse(dependenciesJson);
        if (deps.is_array()) {
            for (const auto& d : deps) {
                if (d.is_string()) {
                    requestedNames.push_back(d.get<std::string>());
                } else if (d.is_object()) {
                    std::string n = d.value("name", "");
                    if (!n.empty()) requestedNames.push_back(std::move(n));
                }
            }
        }
    } catch (...) { /* best-effort attribution */ }

    auto pushError = [&](const std::string& msg) {
        if (requestedNames.empty()) {
            LogosMap e = LogosMap::object();
            e["error"] = msg;
            results.push_back(e);
            return;
        }
        for (const auto& n : requestedNames) {
            LogosMap e = LogosMap::object();
            e["name"]  = n;
            e["error"] = msg;
            results.push_back(e);
        }
    };

    if (!call) {
        pushError(kUnloading);
        return results;
    }

    try {
        // resolveDependenciesJson already returns the per-entry shape we
        // want ({name, version, rootHash, repositoryUrl, url, topLevel}
        // or {name, error}); we just pass it through to the caller.
        LogosList resolved = LogosList::parse(
            call.lib()->resolveDependenciesJson(dependenciesJson, installedPackagesJson));
        for (const auto& entry : resolved) results.push_back(entry);
    } catch (const std::exception& ex) {
        pushError(std::string("resolver exception: ") + ex.what());
    } catch (...) {
        pushError("resolver exception: unknown");
    }
    return results;
}
