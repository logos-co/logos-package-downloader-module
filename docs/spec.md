# Package Downloader Module Specification

## Overview

The Package Downloader module is the Logos platform's in-application gateway to
package distribution. It lets any module or UI plugin **browse a catalog of
available packages across multiple repositories** and **download them**, with
full dependency resolution, repository administration, and persistent
configuration — all reachable over the platform's inter-module RPC, without the
caller ever touching the network, the filesystem, or a CLI.

It is a thin, well-defined bridge in front of an existing capability. The actual
catalog-fetching and download work is performed by a standalone download library
(`lgpd`); this module's job is to expose that library's multi-repository surface
as a process-isolated Logos service that other components can call. It is the
in-app counterpart to the headless `lgpd` command-line tool — same library, same
catalog and repository formats, surfaced to running modules instead of to a
shell.

The module exists so that package-management experiences inside the application
(most directly the package-manager UI's "Manage Repositories" screen, and the
desktop app's module manager) can offer a live, mergeable view of what is
installable and drive downloads — without each consumer re-implementing
repository handling, dependency ordering, or download mechanics.

```
   Package-manager UI ("Manage Repositories")     other modules
                  │                                      │
                  │            LogosAPI / IPC            │
                  ▼                                      ▼
        ┌──────────────────────────────────────────────────────┐
        │            package_downloader module                  │
        │   repository admin · catalog reads · resolve+download │
        └──────────────────────────────────────────────────────┘
                  │ wraps (synchronous, in-process)
                  ▼
        ┌──────────────────────────────────────────────────────┐
        │     download library (lgpd): catalog fetch +          │
        │     dependency resolver + .lgx downloader             │
        └──────────────────────────────────────────────────────┘
                  │ HTTP                          │ disk
                  ▼                               ▼
      remote repositories                 repositories.json
   (logos-repo.json + index.json)          (persisted config)
```

The downloaded artifacts are `.lgx` packages — the platform's package format.
This module only **obtains** packages; installing them is a separate concern
handled by the package manager. A typical end-to-end flow is "this module
downloads, the package manager installs."

---

## Role in the Platform

The Logos platform is built from process-isolated modules (Qt plugins) that talk
to one another over an RPC layer. Each module exposes a set of operations; other
modules call them through the SDK, and UI plugins call them the same way.

This module sits at the **acquisition** edge of package management:

| Concern | Owner |
|---------|-------|
| Discover & download packages from repositories | **this module** |
| Define the catalog / repository file formats | external release tooling |
| Install / register a downloaded package | the package manager |
| Drive a "browse & install" experience | package-manager UI |

Because every operation is reached over RPC, the module runs in its own process:
a malformed catalog, a network stall, or a bad download cannot take down the
caller. The module is a **leaf** in the dependency graph — nothing else in the
platform is built on top of it; it only feeds package-management consumers.

---

## Domain Model

### Concepts

| Term | Definition |
|------|------------|
| **Repository** | A source of packages, identified by the URL of its `logos-repo.json` descriptor (which references an associated `index.json`). One built-in **default** repository is always present; additional **user** repositories are added at runtime. |
| **Default repository** | A hardcoded, always-present repository. It can be enabled/disabled but **cannot be removed**. |
| **User repository** | Any repository added at runtime by URL. Can be removed and enabled/disabled freely. |
| **Catalog** | The merged list of available packages across all *enabled* repositories. Each entry carries its source repository's identity plus the package's metadata and a list of available versions. |
| **Package** | A named, distributable unit (a `.lgx` artifact) with metadata (type, category, author, description, icon) and one or more versions. |
| **Version** | One release of a package. A package's versions are reported newest-first. |
| **rootHash** | A per-build content hash that disambiguates a specific build/variant within a single version. |
| **Pinned download** | A request for one exact build, selected by repository + version + rootHash. Empty fields broaden the match ("any matching"). |
| **Dependency resolution** | Turning a manifest-style dependency list into a concrete, install-ordered set of package builds drawn from the catalog. |
| **Install order** | The order resolution emits entries so that dependencies precede the packages that need them. |
| **Top-level entry** | A resolved entry the caller asked for directly (as opposed to a transitive dependency pulled in by resolution). |
| **Installed set** | An optional list of packages already present on disk, supplied to resolution so it can skip transitive dependencies that are already satisfied. |
| **Catalog-changed event** | A notification the module emits whenever the set of repositories changes, so subscribers know to re-read the catalog. |

### Repository state

Each configured repository carries:

- its **URL** (the identity / primary key),
- **enabled** flag,
- **isDefault** flag (true only for the built-in default),
- descriptive metadata resolved from its descriptor: **name**, **displayName**,
  **description**, **homepage**, **indexUrl**, and a list of **trusted signer
  DIDs**,
- a **resolveError** field that surfaces, per repository, why a fetch failed (so
  one unreachable repository can be reported without failing the others).

The set of repositories — the default plus all user repositories with their
enabled flags — is **persisted** so it survives restarts. Persistence is anchored
to the per-module data directory the host provides; when the module runs outside
such a host (e.g. driven by the CLI or by tests), it falls back to a
conventional per-user configuration location.

### Catalog entry shape

Each catalog entry is the join of *repository identity* and *package metadata*:

```
repository identity   →  repositoryUrl, repositoryName, repositoryDisplayName
package metadata       →  name, type, category, author, description, icon
availability           →  versions[]   (newest-first)
```

The catalog is always the **union across enabled repositories**. Disabling a
repository removes its packages from the merged view without forgetting the
repository itself.

---

## Features & Functional Requirements

### Repository administration

The module is the single authority for the runtime repository list. It must
support:

- **Adding** a user repository by the URL of its `logos-repo.json`.
- **Removing** a user repository. Removing the built-in default is refused (an
  error is returned, the default stays).
- **Enabling / disabling** any repository, which controls whether its packages
  appear in the merged catalog.
- **Listing** all configured repositories with their full state (enabled,
  isDefault, resolved descriptor metadata, and any per-repository resolve
  error).

Every successful mutation (add / remove / enable-disable) persists the new
configuration and emits the **catalog-changed event** so interested UIs refresh.

### Catalog browsing

- **Full catalog** — the merged, deduplicated view across every enabled
  repository, each entry tagged with its source repository.
- **Per-repository catalog** — the same shape, scoped to a single repository
  identified by either its URL or its name.
- **Refresh** — re-fetch every enabled repository's descriptor and index so the
  catalog reflects current upstream state.

### Dependency-aware resolution and download

The module turns a **manifest-style dependency list** — a list of either bare
package names or `{name, version?, signer?}` objects — into concrete actions:

- **Resolve (preview, no download)** — produce the install-ordered set of
  package builds that satisfying the request would require, each annotated with
  the version, rootHash, repository, download URL, and whether it is a top-level
  request or a transitive dependency. Optionally, an **installed set** can be
  supplied so resolution omits transitive dependencies already present on disk.
  This is the "what would change" preview a UI shows before committing.

- **Resolve and download** — perform the same resolution and then download every
  resulting entry in install order, returning one result row per package.

- **Pinned download** — download one exact build chosen by repository, package
  name, version, and rootHash. Empty fields broaden the selection: empty
  repository means *any enabled repository*, empty version means *newest*, empty
  rootHash means *do not disambiguate further*.

### Resilience

Catalog data comes from external repositories and can be malformed or stale. The
resolve/download operations are required to be **batch-safe**: a failure or
exception while handling one requested package is converted into a per-package
**error row** attributed to the package(s) the caller asked for, rather than
aborting the whole operation or producing an unattributable failure. This matters
because consumers (notably the UI) track per-package state by name — a nameless
failure would leave a row stuck without feedback.

### Non-blocking contract

All of the module's operations are conceptually synchronous request/response
calls. The platform runtime presents them to callers as non-blocking
asynchronous calls over RPC, so a caller is never blocked while a network fetch
or download is in flight. Callers observe a normal request/response with a
structured result.

---

## Operations

All operations are reachable over the platform RPC layer. Mutating operations
return a result object of the shape `{ success, error? }` (error present only on
failure). Read operations return lists or maps of domain data.

| Operation | Result | Behavior |
|-----------|--------|----------|
| **Add repository** | `{success, error?}` | Register a user repository by its `logos-repo.json` URL. On success, persist and emit *catalog-changed*. |
| **Remove repository** | `{success, error?}` | Remove a user repository. Refused for the built-in default (returns an error). On success, persist and emit *catalog-changed*. |
| **Set repository enabled** | `{success, error?}` | Enable or disable a repository. On success, persist and emit *catalog-changed*. |
| **List repositories** | list of repository states | Report every configured repository: `{url, enabled, isDefault, name, displayName, description, homepage, indexUrl, trustedSignerDids[], resolveError}`. |
| **Refresh catalog** | `{success, error?}` | Re-fetch every enabled repository's descriptor + index. Surfaces any fetch error. |
| **Get catalog** | list of catalog entries | Merged catalog across all enabled repositories. Each entry: `{repositoryUrl, repositoryName, repositoryDisplayName, name, type, category, author, description, icon, versions[]}` (versions newest-first). |
| **Get catalog for repository** | list of catalog entries | Same shape, scoped to one repository identified by URL or name. |
| **Resolve dependencies** | list of resolved entries | Download-free preview. Resolve a manifest-style dependency list into install-ordered entries `{name, version, rootHash, repositoryUrl, url, topLevel}`. Accepts an optional installed-set so already-satisfied transitive deps are omitted. |
| **Download resolved dependencies** | list of result rows | Resolve a manifest-style dependency list, then download every entry in install order. Each row: `{name, path, error?}`. |
| **Download pinned** | result row | Download one exact build chosen by `{repository, name, version, rootHash}`; empty fields broaden the match. Returns `{name, path, error?}` (plus version/rootHash/repositoryUrl when supplied). |

### Event

| Event | Meaning |
|-------|---------|
| **Catalog changed** | Emitted after any successful repository mutation (add / remove / enable-disable). Subscribers re-read the repository list and catalog. |

---

## Use Cases & Workflows

### Browse-and-install from a UI

```
1. add repository(s)            → user points the app at extra catalogs
2. refresh catalog              → fetch current upstream state
3. get catalog                  → populate the package list
   └─ on any later repo change, the catalog-changed event fires →
      the UI re-reads list repositories / get catalog automatically
```

The UI never polls: it reads once and then reacts to the catalog-changed event.

### Preview, then commit, an install

```
1. resolve dependencies(request, installed-set)
   → install-ordered plan with top-level vs. transitive flags, NO download
   → the user reviews exactly what will be fetched
2. download resolved dependencies(request)
   → fetches each entry in install order, returns one row per package
3. for each successful row, hand its .lgx path to the package manager to install
```

The installed-set lets step 1 skip dependencies the user already has, so the plan
only shows the genuinely new work.

### Pinned / exact acquisition

```
download pinned(repository, name, version, rootHash)
   → fetch one specific build
   → empty repository ⇒ any enabled repo; empty version ⇒ newest;
     empty rootHash ⇒ no further disambiguation
```

Used when a caller already knows precisely which build it wants (e.g. pinning a
known-good version), rather than resolving from a manifest.

### Repository administration

```
add repository / remove repository / set repository enabled
   → mutate the persisted repository configuration
   → emit catalog-changed on success
list repositories
   → report current state, including a per-repository resolveError when a
     repository's descriptor could not be fetched
```

---

## Behavioral Contracts & Guarantees

- **The default repository is permanent.** It is always present and removal is
  refused; only its enabled state can change.
- **The catalog is always the live union of enabled repositories.** Disabling a
  repository hides its packages without discarding the repository's
  configuration.
- **Repository configuration is durable.** The repository set survives restarts,
  anchored to the host-provided per-module data directory (with a per-user
  fallback when no host provisions one).
- **Mutations notify.** Every successful add/remove/enable emits the
  catalog-changed event so dependent views stay consistent.
- **Resolution preview never downloads.** "Resolve dependencies" has no side
  effects beyond reading catalog data; it is safe to call to show a plan.
- **Batch operations are fault-isolated.** A failure handling one requested
  package becomes a per-package error row attributed by name; the rest of the
  batch proceeds, and no failure is left unattributable when the caller named the
  package.
- **Empty pinning fields mean "any."** In a pinned download, an empty repository,
  version, or rootHash widens the selection rather than failing.
- **Calls don't block the caller.** Operations are synchronous in concept but
  delivered as non-blocking asynchronous RPC by the runtime.

---

## Relationship to the `lgpd` CLI

This module and the `lgpd` command-line tool are two front-ends over the same
download library and the same repository/catalog formats:

| | `lgpd` CLI | Package Downloader module |
|---|---|---|
| Audience | a shell / operator | running modules & UI plugins |
| Invocation | command line | platform RPC |
| Repository config | user repos via the CLI's `repo` commands | user repos via this module's operations |
| Catalog & repo formats | identical (`logos-repo.json` / `index.json`) | identical |

The repository configuration both write is the same on-disk format, so the two
front-ends present a consistent view of catalogs and repositories. The
`logos-repo.json` / `index.json` formats themselves are specified by the external
release tooling, not by this module.

---

## Out of Scope

- **Installing packages.** This module downloads `.lgx` artifacts; registering and
  activating them is the package manager's responsibility.
- **Defining catalog / repository formats.** The `logos-repo.json` and
  `index.json` schemas are owned by the external release tooling; this module
  consumes them.
- **Signing / publishing.** Producing or signing repositories and packages is not
  part of this module.
- **Synchronous blocking I/O for callers.** Network and disk work happens behind
  the asynchronous RPC boundary; callers do not block on it.
