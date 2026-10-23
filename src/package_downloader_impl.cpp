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
#include <optional>
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

// The answer to a call that arrives before start(), or after stop().
const char* const kNotStarted = "the downloader is not started: call start() first";

// Why a stop() fails the storage downloads in flight.
const char* const kStopped = "the downloader was stopped";

// The states getState() answers and stateChanged reports.
const char* const kStateRunning = "running";
const char* const kStateStopped = "stopped";

// The states getStorageState() answers and storageStateChanged reports.
const char* const kStorageOff = "off";
const char* const kStorageWaiting = "waiting";
const char* const kStorageStarting = "starting";
const char* const kStorageRunning = "running";
const char* const kStorageFailed = "failed";

// Where a download's byte counts go. Empty means "report nothing"; the lib
// still runs curl's transfer callback to check for unload cancellation.
using ProgressSink = std::function<void(const std::string& packageName,
                                        std::uint64_t received,
                                        std::uint64_t total,
                                        const std::string& transport)>;

// Single-shot download with full (repo, version, hash) pinning. Used by
// `downloadPinned` and `downloadResolvedDependencies` below. Kept as a
// free function rather than a method so it can be reused without the
// codegen seeing overload ambiguity.
LogosMap pinnedDownload(lgpd::PackageDownloaderLib* lib,
                        const std::string& repoUrlOrName,
                        const std::string& packageName,
                        const std::string& version,
                        const std::string& rootHash,
                        const ProgressSink& onProgress,
                        const lgpd::CancelFn& isCancelled) {
    LogosMap result = LogosMap::object();
    result["name"] = packageName;

    // Set before each attempt, read by that attempt's samples: the lib calls
    // both on this thread or under a transfer it has started since.
    std::mutex transportMutex;
    std::string transport;

    lgpd::ProgressFn progressFn;
    lgpd::TransportFn transportFn;
    if (onProgress) {
        progressFn = [&](std::uint64_t received, std::uint64_t total) {
            std::string current;
            {
                std::lock_guard<std::mutex> lock(transportMutex);
                current = transport;
            }
            onProgress(packageName, received, total, current);
        };
        transportFn = [&](const std::string& next) {
            std::lock_guard<std::mutex> lock(transportMutex);
            transport = next;
        };
    }

    std::string err;
    std::string source;
    std::string path = lib->downloadPackage(repoUrlOrName, packageName, err,
                                            version, rootHash, "", progressFn, &source,
                                            isCancelled, transportFn);

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

struct PackageDownloaderImpl::Run {
    std::shared_ptr<lgpd::PackageDownloaderLib> lib;

    // The lib's catalogRevision() last announced with catalogChanged.
    std::atomic<std::uint64_t> announcedCatalogRevision{0};

    // Null outside a framework context: no storage_module to reach.
    std::shared_ptr<StorageFetcher> storageFetcher;

    // Set once, by stop(). Read without the lock by the downloads' cancel checks.
    std::atomic<bool> stopped{false};

    std::mutex mutex;
    std::function<void()> cancelWatch;

    // Keeps the storage watch in place of any earlier one, or cancels it at
    // once if the run already stopped.
    void watch(std::function<void()> cancel) {
        std::function<void()> previous;
        {
            std::lock_guard<std::mutex> lock(mutex);

            if (!stopped) {
                previous = std::exchange(cancelWatch, std::move(cancel));
                cancel = nullptr;
            }
        }

        if (previous) {
            previous();
        }

        if (cancel) {
            cancel();
        }
    }

    void unwatch() {
        std::function<void()> cancel;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cancel = std::move(cancelWatch);
        }

        if (cancel) {
            cancel();
        }
    }

    void stop(const std::string& reason) {
        std::function<void()> cancel;
        {
            std::lock_guard<std::mutex> lock(mutex);

            if (stopped) {
                return;
            }

            stopped = true;
            cancel = std::move(cancelWatch);
        }

        if (cancel) {
            cancel();
        }

        if (storageFetcher) {
            storageFetcher->cancelPendingDownloads(reason);
        }
    }
};

struct PackageDownloaderImpl::CallState {
    std::mutex mutex;

    // Null while stopped. A call keeps its own share past a stop().
    std::shared_ptr<Run> run;
    int pending = 0;

    // Set by aboutToUnload(): new calls are refused and no event is emitted.
    std::atomic<bool> unloading{false};

    // Set when aboutToUnload() returned Asynchronous; the destructor clears it.
    std::function<void()> onDrained;

    // A setDownloadSource() before start(), saved by start().
    std::optional<lgpd::DownloadSource> pendingSource;

    std::string storageState = kStorageOff;
    std::string storageError;
};

class PackageDownloaderImpl::PendingLibCall {
public:
    explicit PendingLibCall(PackageDownloaderImpl& impl);
    ~PendingLibCall();

    // False for a call that must not start: unloading, or not started.
    explicit operator bool() const { return m_run != nullptr; }

    // Why operator bool is false.
    const char* refusal() const { return m_counted ? kNotStarted : kUnloading; }

    // True when the call counts towards aboutToUnload()'s wait.
    bool counted() const { return m_counted; }

    const std::shared_ptr<Run>& run() const { return m_run; }

    lgpd::PackageDownloaderLib* lib() const { return m_run->lib.get(); }

    // Once true, the call emits nothing: the host tears the event path down.
    bool unloading() const { return m_state->unloading; }

    // Once true, the call's downloads stop.
    bool cancelled() const { return m_state->unloading || (m_run && m_run->stopped); }

private:
    std::shared_ptr<CallState> m_state;
    std::shared_ptr<Run> m_run;
    bool m_counted = false;
};

PackageDownloaderImpl::PackageDownloaderImpl()
    : m_calls(std::make_shared<CallState>())
{
}

// The host runs this at process exit, after its grace period: a call may still
// be running, and keeps the lib alive through its own share.
PackageDownloaderImpl::~PackageDownloaderImpl() {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        m_calls->onDrained = nullptr;
        run = std::move(m_calls->run);
    }

    if (run) {
        run->stop(kUnloading);
    }
}

LogosMap PackageDownloaderImpl::start() {
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);

    // Counted, so that aboutToUnload() waits for a start in progress.
    PendingLibCall call(*this);

    if (!call.counted()) {
        return makeResult(kUnloading);
    }

    if (call) {
        return makeResult("");
    }

    // The host's per-instance directory, or an XDG one outside a host (lgpd
    // CLI, unit tests), where instancePersistencePath() is empty.
    const std::string configPath = instancePersistencePath().empty()
        ? defaultConfigPath()
        : (fs::path(instancePersistencePath()) / "repositories.json").string();

    auto run = std::make_shared<Run>();
    run->lib = std::make_shared<lgpd::PackageDownloaderLib>(configPath);

    std::optional<lgpd::DownloadSource> pendingSource;
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);
        pendingSource = std::exchange(m_calls->pendingSource, std::nullopt);
    }

    if (pendingSource) {
        const std::string err = run->lib->registry().setDownloadSource(*pendingSource);

        if (!err.empty()) {
            fprintf(stderr, "PackageDownloaderImpl::start: could not save the download source: %s\n",
                    err.c_str());
        }
    }

    // Before any call is served: setStorageFetcher then never waits on the
    // lib's lock, which a first catalog fetch holds across the network.
    if (isContextReady()) {
        run->storageFetcher = makeStorageFetcher(modules());
        run->lib->setStorageFetcher(run->storageFetcher);
    }

    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        if (m_calls->unloading) {
            return makeResult(kUnloading);
        }

        m_calls->run = run;
    }

    if (run->lib->registry().downloadSource() != lgpd::DownloadSource::Http) {
        watchStorage(run);
    }

    if (!call.unloading()) {
        stateChanged(kStateRunning);
    }

    return makeResult("");
}

LogosMap PackageDownloaderImpl::stop() {
    std::lock_guard<std::mutex> lifecycle(m_lifecycleMutex);

    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        if (m_calls->unloading) {
            return makeResult(kUnloading);
        }

        run = std::move(m_calls->run);
    }

    if (run) {
        run->stop(kStopped);

        if (!m_calls->unloading) {
            stateChanged(kStateStopped);
        }

        setStorageState(kStorageOff, "");
    }

    return makeResult("");
}

std::string PackageDownloaderImpl::getState() {
    std::lock_guard<std::mutex> lock(m_calls->mutex);

    return m_calls->run && !m_calls->unloading ? kStateRunning : kStateStopped;
}

void PackageDownloaderImpl::startStorage(const std::shared_ptr<Run>& run) {
    // Kept until the last reply: aboutToUnload() waits for it.
    auto call = std::make_shared<PendingLibCall>(*this);

    // Stopped, or restarted, since the watch fired; or switched to "http".
    if (!*call || call->run() != run
        || run->lib->registry().downloadSource() == lgpd::DownloadSource::Http) {
        return;
    }

    // storage_module's ready can arrive twice for one load (the edge and the
    // armed replay). A second start while the first is under way is refused as
    // "busy", which is no failure.
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        if (m_calls->storageState == kStorageStarting) {
            return;
        }

        m_calls->storageState = kStorageStarting;
        m_calls->storageError.clear();
    }

    if (!call->unloading()) {
        storageStateChanged(kStorageStarting, "");
    }

    if (run->storageFetcher) {
        run->storageFetcher->subscribe();
    }

    StorageNode node = makeStorageNode(modules());
    node.stopped = [call]() { return call->cancelled(); };

    startStorageNode(node, [this, call, run](const std::string& error) {
        if (!error.empty()) {
            fprintf(stderr, "PackageDownloaderImpl::startStorage: %s\n", error.c_str());
        }

        if (call->cancelled()) {
            return;
        }

        if (!error.empty()) {
            setStorageState(kStorageFailed, error);
            return;
        }

        // Accepted is not up: storage_module finishes the start later and
        // says so with storageStart. A node that was running already says
        // nothing, so ask it.
        confirmStorageRunning(run);
    });
}

// Moves to `to` only from one of `from`: a late event must not overwrite a
// state reached since.
bool PackageDownloaderImpl::moveStorageState(std::initializer_list<const char*> from,
                                             const char* to, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        if (m_calls->unloading) {
            return false;
        }

        const bool allowed = std::any_of(from.begin(), from.end(), [&](const char* s) {
            return m_calls->storageState == s;
        });

        if (!allowed || (m_calls->storageState == to && m_calls->storageError == error)) {
            return false;
        }

        m_calls->storageState = to;
        m_calls->storageError = error;
    }

    storageStateChanged(to, error);
    return true;
}

void PackageDownloaderImpl::confirmStorageRunning(const std::shared_ptr<Run>& run) {
    auto call = std::make_shared<PendingLibCall>(*this);

    if (!*call || call->run() != run || !isContextReady()) {
        return;
    }

    StorageNode node = makeStorageNode(modules());
    node.isRunning([this, call](bool running) {
        if (running && !call->cancelled()) {
            moveStorageState({kStorageStarting}, kStorageRunning, "");
        }
    });
}

void PackageDownloaderImpl::storageStarted(const std::shared_ptr<Run>& run,
                                           const std::string& payload) {
    PendingLibCall call(*this);

    if (!call || call.run() != run
        || run->lib->registry().downloadSource() == lgpd::DownloadSource::Http) {
        return;
    }

    bool success = false;
    std::string message;

    // On the module's event thread: nothing may escape.
    try {
        const LogosMap event = LogosMap::parse(payload);

        if (event.is_object()) {
            success = event.value("success", false);
            message = event.value("message", "");
        }
    } catch (...) {
    }

    if (success) {
        // From failed too: another consumer may have brought it up since.
        moveStorageState({kStorageStarting, kStorageWaiting, kStorageFailed}, kStorageRunning, "");
    } else {
        moveStorageState({kStorageStarting}, kStorageFailed,
                         message.empty() ? std::string("the storage node did not start") : message);
    }
}

// Stopped (the Storage app, say), unloaded or crashed while in use.
void PackageDownloaderImpl::storageStopped(const std::shared_ptr<Run>& run) {
    PendingLibCall call(*this);

    if (!call || call.run() != run
        || run->lib->registry().downloadSource() == lgpd::DownloadSource::Http) {
        return;
    }

    moveStorageState({kStorageRunning, kStorageStarting}, kStorageFailed,
                     "the storage node stopped");
}

// Waits for storage_module to be ready, then starts the node. Again after a
// switch away from "http": the watch's armed replay starts it at once if
// storage_module is ready already.
void PackageDownloaderImpl::watchStorage(const std::shared_ptr<Run>& run) {
    if (!isContextReady()) {
        return;
    }

    setStorageState(kStorageWaiting, "");

    // Weak: the watch's callback may outlive its cancellation.
    std::weak_ptr<Run> weakRun = run;

    std::function<void()> cancelReady = watchStorageReady(
        modules(),
        [this, weakRun]() {
            if (std::shared_ptr<Run> current = weakRun.lock()) {
                startStorage(current);
            }
        },
        [this, weakRun]() {
            if (std::shared_ptr<Run> current = weakRun.lock()) {
                storageStopped(current);
            }
        });

    std::function<void()> cancelNode = watchStorageNode(
        modules(),
        [this, weakRun](const std::string& payload) {
            if (std::shared_ptr<Run> current = weakRun.lock()) {
                storageStarted(current, payload);
            }
        },
        [this, weakRun](const std::string&) {
            if (std::shared_ptr<Run> current = weakRun.lock()) {
                storageStopped(current);
            }
        });

    run->watch([cancelReady, cancelNode]() {
        if (cancelReady) cancelReady();
        if (cancelNode) cancelNode();
    });
}

void PackageDownloaderImpl::setStorageState(const std::string& state, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        if (m_calls->unloading
            || (m_calls->storageState == state && m_calls->storageError == error)) {
            return;
        }

        m_calls->storageState = state;
        m_calls->storageError = error;
    }

    storageStateChanged(state, error);
}

LogosMap PackageDownloaderImpl::getStorageState() {
    LogosMap r = LogosMap::object();
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        r["state"] = m_calls->storageState;
        r["error"] = m_calls->storageError;

        if (m_calls->storageState == kStorageStarting) {
            run = m_calls->run;
        }
    }

    // A storageStart missed (sent before the subscription armed) would hold
    // "starting" for good: whoever is waiting on it asks again, and this
    // checks the node meanwhile.
    if (run) {
        confirmStorageRunning(run);
    }

    return r;
}

LogosShutdown PackageDownloaderImpl::aboutToUnload() {
    std::shared_ptr<Run> run;
    {
        std::lock_guard<std::mutex> lock(m_calls->mutex);

        m_calls->unloading = true;
        run = m_calls->run;
    }

    if (run) {
        run->stop(kUnloading);
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
    m_counted = true;
    m_run = m_state->run;
}

PackageDownloaderImpl::PendingLibCall::~PendingLibCall() {
    if (!m_counted) {
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
    if (!call) return makeResult(call.refusal());
    const std::string err = call.lib()->registry().addRepository(url);
    if (err.empty() && !call.unloading()) catalogChanged();
    return makeResult(err);
}

LogosMap PackageDownloaderImpl::removeRepository(const std::string& url) {
    PendingLibCall call(*this);
    if (!call) return makeResult(call.refusal());
    const std::string err = call.lib()->registry().removeRepository(url);
    if (err.empty() && !call.unloading()) catalogChanged();
    return makeResult(err);
}

LogosMap PackageDownloaderImpl::setRepositoryEnabled(const std::string& url, bool enabled) {
    PendingLibCall call(*this);
    if (!call) return makeResult(call.refusal());
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
    if (!call) return makeResult(call.refusal());
    LogosMap result = makeResult(call.lib()->refreshCatalogs());
    announceCatalogRevision(call);
    return result;
}

LogosList PackageDownloaderImpl::getCatalog() {
    PendingLibCall call(*this);
    if (!call) return LogosList::array();
    LogosList catalog = LogosList::parse(call.lib()->getCatalogJson());
    announceCatalogRevision(call);
    return catalog;
}

LogosList PackageDownloaderImpl::getCatalogForRepo(const std::string& repoUrlOrName) {
    PendingLibCall call(*this);
    if (!call) return LogosList::array();
    LogosList catalog = LogosList::parse(call.lib()->getCatalogForRepoJson(repoUrlOrName));
    announceCatalogRevision(call);
    return catalog;
}

// Once per revision, however many concurrent calls saw the lib move to it.
void PackageDownloaderImpl::announceCatalogRevision(const PendingLibCall& call) {
    if (call.unloading()) return;
    const std::uint64_t revision = call.lib()->catalogRevision();
    std::atomic<std::uint64_t>& announced = call.run()->announcedCatalogRevision;
    std::uint64_t seen = announced.load();
    while (seen < revision) {
        if (announced.compare_exchange_weak(seen, revision)) {
            catalogChanged();
            return;
        }
    }
}

std::string PackageDownloaderImpl::getDownloadSource() {
    PendingLibCall call(*this);
    if (!call) {
        std::lock_guard<std::mutex> lock(m_calls->mutex);
        return m_calls->pendingSource ? lgpd::downloadSourceName(*m_calls->pendingSource)
                                      : std::string();
    }
    return lgpd::downloadSourceName(call.lib()->registry().downloadSource());
}

LogosMap PackageDownloaderImpl::setDownloadSource(const std::string& source) {
    PendingLibCall call(*this);
    if (!call && !call.counted()) return makeResult(call.refusal());

    const auto parsed = lgpd::parseDownloadSource(source);
    if (!parsed) {
        return makeResult("unknown download source '" + source + "': expected any, logos or http");
    }

    if (!call) {
        std::lock_guard<std::mutex> lock(m_calls->mutex);
        m_calls->pendingSource = *parsed;
        return makeResult("");
    }

    lgpd::RepositoryRegistry& registry = call.lib()->registry();
    const bool changed = registry.downloadSource() != *parsed;
    const std::string err = registry.setDownloadSource(*parsed);

    if (!err.empty() || !changed || call.unloading()) {
        return makeResult(err);
    }

    // The catalog's availability marks follow the source.
    catalogChanged();

    // The node is let go of, not stopped: another consumer may be using it.
    if (*parsed == lgpd::DownloadSource::Http) {
        call.run()->unwatch();
        setStorageState(kStorageOff, "");
    } else {
        std::string state;
        {
            std::lock_guard<std::mutex> lock(m_calls->mutex);
            state = m_calls->storageState;
        }

        if (state == kStorageOff) {
            watchStorage(call.run());
        }
    }

    return makeResult("");
}

LogosMap PackageDownloaderImpl::downloadPinned(const std::string& repoUrlOrName,
                                                const std::string& packageName,
                                                const std::string& version,
                                                const std::string& rootHash) {
    PendingLibCall call(*this);

    if (!call) {
        LogosMap refused = LogosMap::object();
        refused["name"] = packageName;
        refused["error"] = call.refusal();
        return refused;
    }

    LogosMap result = pinnedDownload(call.lib(), repoUrlOrName, packageName, version, rootHash,
                                     [this, &call](const std::string& name, std::uint64_t received,
                                                   std::uint64_t total, const std::string& transport) {
                                         if (!call.unloading()) {
                                             downloadProgress(name, received, total, transport);
                                         }
                                     },
                                     [&call]() { return call.cancelled(); });

    // A successful download contains the path where the package was downloaded.
    if (result.contains("path") && !call.unloading()) {
        downloadDone(packageName, result.value("source", ""));
    }

    announceCatalogRevision(call);
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
        pushError(call.refusal());
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

            // Unloading or stopped: start no further package.
            if (call.cancelled()) {
                LogosMap e = LogosMap::object();
                e["name"]  = name;
                e["error"] = call.unloading() ? kUnloading : kStopped;
                results.push_back(e);
                break;
            }

            LogosMap downloaded = pinnedDownload(
                call.lib(), repoUrl, name, version, rootHash,
                [this, &call](const std::string& pkg, std::uint64_t received,
                              std::uint64_t total, const std::string& transport) {
                    if (!call.unloading()) {
                        downloadProgress(pkg, received, total, transport);
                    }
                },
                [&call]() { return call.cancelled(); });

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
    announceCatalogRevision(call);
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
        pushError(call.refusal());
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
    announceCatalogRevision(call);
    return results;
}
