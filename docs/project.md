# logos-package-downloader-module — Project Description

## Overview

`logos-package-downloader-module` is a process-isolated Logos **core** module (a Qt 6
plugin) that bridges the standalone [`logos-package-downloader`](https://github.com/logos-co/logos-package-downloader)
C++17 library — the `lgpd` namespace / `lgpd` CLI, plain C++ + libcurl, **no Qt** — into the
Logos platform. It surfaces that library's multi-repository catalog browsing and `.lgx`
package download capabilities to other modules and to UI plugins (notably the
package-manager "Manage Repositories" UI) over `LogosAPI`/IPC. The module is the in-app,
IPC-callable counterpart to the headless `lgpd` CLI documented in the workspace `CLAUDE.md`.

The module's own implementation class, `PackageDownloaderImpl` (in
`src/package_downloader_impl.{h,cpp}`), is itself pure C++: it inherits
`LogosModuleContext` and uses `std::string` / `std::vector` plus `LogosMap` / `LogosList`
(aliases for `nlohmann::json` from `logos_json.h`) rather than Qt types. It holds a single
`lgpd::PackageDownloaderLib*` and mostly forwards calls, parsing the library's JSON-string
returns into `LogosList` / `LogosMap` and shaping mutating-call results as
`{ success: bool, error?: string }`. The Qt plugin glue (the `_plugin` / provider layer,
MOC, async IPC wrappers) is **not** hand-written — it is auto-generated at build time by
`logos-cpp-generator --from-header`, driven by `"interface": "universal"` in
`metadata.json`. All library calls are synchronous; the Logos runtime auto-generates
`*Async` wrappers for IPC so callers never block.

### Place in the Logos dependency chain

This is a **leaf** module. Its `flake.nix` pulls in `logos-module-builder` (which
transitively brings `logos-cpp-sdk` and `logos-module`) and `logos-package-downloader`
(staged into `lib/` as an external library and linked into the plugin). It feeds the
package-manager UI and any consumer needing per-repo / per-version / per-signer downloads.

```
nixpkgs (pinned by logos-cpp-sdk)
  └─ logos-cpp-sdk         (LogosAPI, IPC, LogosModuleContext, logos-cpp-generator)
       └─ logos-module     (PluginInterface introspection — used by `lm`)
       └─ logos-module-builder   (mkLogosModule, LogosModule.cmake, glue codegen)
            └─ logos-package-downloader-module   ◄── THIS REPO
                 │   links ─► logos-package-downloader  (lgpd lib: plain C++ + libcurl)
                 │
                 └─ consumed by ─► logos-package-manager-ui ("Manage Repositories")
                                   logos-basecamp app manager
                                   any module that downloads packages
```

The `logos-repo.json` / `index.json` catalog formats this module reads are specified
externally in
[logos-modules-release-tool/docs/catalog-format.md](https://github.com/logos-co/logos-modules-release-tool/blob/main/docs/catalog-format.md).

## Project Structure

```
logos-package-downloader-module/
├── metadata.json                       # Single source of truth: name, version, type,
│                                       #   interface=universal, capabilities, nix settings
├── flake.nix                           # Nix build via logos-module-builder.lib.mkLogosModule
├── flake.lock                          # Pins logos-module-builder + logos-package-downloader
├── CMakeLists.txt                      # logos_module() macro: NAME, SOURCES, EXTERNAL_LIBS
├── README.md                           # API reference + usage from another module
├── LICENSE-APACHE-v2 / LICENSE-MIT     # Dual license
├── src/
│   ├── package_downloader_impl.h       # Bridge interface: PackageDownloaderImpl
│   │                                   #   (inherits LogosModuleContext), method decls
│   │                                   #   (one per line — codegen requirement),
│   │                                   #   logos_events: catalogChanged, onContextReady()
│   └── package_downloader_impl.cpp     # Implementation: holds lgpd::PackageDownloaderLib*,
│                                       #   JSON parsing, {success,error} shaping,
│                                       #   pinnedDownload helper, exception-fenced
│                                       #   resolve/download, XDG/persistence anchoring
├── tests/
│   ├── CMakeLists.txt                  # logos_test() target package_downloader_module_tests
│   ├── main.cpp                        # LOGOS_TEST_MAIN() entry point
│   ├── test_package_downloader.cpp     # Bridge-logic unit tests
│   ├── package_downloader_events_test.cpp  # Qt-free body for the catalogChanged event
│   ├── stubs/
│   │   └── package_downloader_lib.h    # Minimal stub of the lgpd surface the impl calls
│   └── mocks/
│       └── mock_package_downloader_lib.cpp  # Link-time mock: records calls, returns
│                                            #   test-configured JSON / error strings
├── docs/
│   ├── index.md                        # Docs index (→ spec.md, project.md)
│   ├── spec.md                         # Business / domain specification
│   └── project.md                      # This document
└── .github/
    └── workflows/
        └── ci.yml                      # CI: ubuntu + macos; nix build + nix build .#unit-tests
```

Note: the Qt plugin glue (`package_downloader_plugin`, the provider object, MOC output,
async IPC wrappers, the per-build `LogosModules` aggregate, and the
`catalogChanged` event sidecar) is generated into `generated_code/` at build time by
`logos-cpp-generator --from-header` — it is not checked into the repo. `LogosModule.cmake`
picks up `generated_code/` automatically for universal modules.

## Technology Stack

| Component | Type | Purpose |
|-----------|------|---------|
| **C++17** | Language | Implementation language (the impl is Qt-free) |
| **Qt 6** | Framework | The produced artifact is a Qt plugin; glue is codegen-emitted. Version is fixed by `logos-cpp-sdk` |
| **nlohmann::json** | Library | JSON parsing/serialization via `logos_json.h`; `LogosMap` / `LogosList` are aliases |
| **CMake ≥ 3.14** | Build system | Drives the plugin build via the `logos_module()` macro |
| **Nix flakes** | Build / packaging | Reproducible builds; `mkLogosModule` derivation |
| **libcurl** | Library (transitive) | HTTP fetch, inside `logos-package-downloader` (not linked directly here) |
| **LogosTest** | Test framework | `logos_test.h`, link-time C-library mocking (`MOCK_C_SOURCES`) |

### Dependencies

Direct flake inputs (`flake.nix`):

| Input | Type | Purpose |
|-------|------|---------|
| **logos-module-builder** | Direct flake input | Provides `mkLogosModule` (nix build), `LogosModule.cmake` / the `logos_module()` macro, and the Qt-glue generator (universal-interface codegen). Transitively brings in `logos-cpp-sdk` and `logos-module` |
| **logos-package-downloader** | Direct flake input | The underlying plain-C++17 + libcurl download library (`lgpd` namespace). Its `lib` output (`libpackage_downloader_lib.{so,dylib}` + headers) is staged into `lib/` and linked into the plugin |

Resolved transitively through the builder:

| Input | Type | Purpose |
|-------|------|---------|
| **logos-cpp-sdk** | Transitive | `LogosAPI` / IPC, `LogosModuleContext`, `LogosMap`/`LogosList` aliases (`logos_json.h`), and `logos-cpp-generator` used to generate the plugin glue + async wrappers |
| **logos-module** | Transitive | Plugin introspection library providing `PluginInterface`; enables `lm` to inspect the built plugin's metadata and methods |

The `flake.lock` root pins exactly two inputs (`logos-module-builder`,
`logos-package-downloader`); nixpkgs/Qt are resolved transitively through the
builder/SDK.

### metadata.json (single source of truth)

```json
{
    "name": "package_downloader",
    "version": "1.0.0",
    "description": "Online package catalog and download service",
    "author": "Logos Core Team",
    "type": "core",
    "interface": "universal",
    "category": "management",
    "main": "package_downloader_plugin",
    "dependencies": [],
    "include": ["libpackage_downloader_lib.so", "libpackage_downloader_lib.dylib"],
    "capabilities": ["package_download"],
    "nix": {
        "external_libraries": [{ "name": "package_downloader", "build_command": "true" }],
        "cmake": { "extra_include_dirs": ["lib"] }
    }
}
```

`"interface": "universal"` is the load-bearing field: it tells `logos-cpp-generator` to
generate the Qt plugin glue + async IPC wrappers from the plain-C++ impl header
(`--from-header`) at build time. `dependencies` is empty — the module has no Logos-module
dependencies; it links the `package_downloader` external library instead.

## Component: PackageDownloaderImpl

**Files:** `src/package_downloader_impl.h`, `src/package_downloader_impl.cpp`

The single bridge class. Inherits `LogosModuleContext` (the opt-in SDK mixin that exposes
`modulePath()` / `instanceId()` / `instancePersistencePath()` and the `onContextReady()`
hook). Holds one `lgpd::PackageDownloaderLib* m_lib` and forwards method calls into the
library / its `RepositoryRegistry`, translating between JSON strings and `LogosList` /
`LogosMap`.

Every public method on this class is an IPC-callable module method (the codegen turns each
single-line declaration in the header into a provider method plus an auto-generated
`*Async` wrapper). All return shapes below are the JSON shapes the impl produces; over
`LogosAPI` / QML they surface as `QVariantMap` (for `LogosMap`) and `QVariantList`
(for `LogosList`).

### Public API

| Method | Signature | Return shape / behavior |
|--------|-----------|-------------------------|
| `addRepository` | `LogosMap addRepository(const std::string& url)` | Add a user repository by its `logos-repo.json` URL. Forwards to `registry().addRepository(url)`. Returns `{success, error?}`. Emits `catalogChanged` on success |
| `removeRepository` | `LogosMap removeRepository(const std::string& url)` | Remove a user repository (the built-in default cannot be removed). Forwards to `registry().removeRepository(url)`. Returns `{success, error?}`. Emits `catalogChanged` on success |
| `setRepositoryEnabled` | `LogosMap setRepositoryEnabled(const std::string& url, bool enabled)` | Enable/disable a repository. Forwards to `registry().setEnabled(url, enabled)`. Returns `{success, error?}`. Emits `catalogChanged` on success |
| `listRepositories` | `LogosList listRepositories()` | Configured repos. Each entry: `{url, enabled, isDefault, name, displayName, description, homepage, indexUrl, trustedSignerDids[], resolveError}`. Parses `m_lib->listRepositoriesJson()` |
| `refreshCatalog` | `LogosMap refreshCatalog()` | Re-fetch every enabled repo's `logos-repo.json` + `index.json`. Returns `{success, error?}` — error string surfaced from `m_lib->refreshCatalogs()` |
| `getCatalog` | `LogosList getCatalog()` | Merged catalog across all enabled repos. Each entry: `{repositoryUrl, repositoryName, repositoryDisplayName, name, type, category, author, description, icon, versions[]}` (versions newest-first). Parses `m_lib->getCatalogJson()` |
| `getCatalogForRepo` | `LogosList getCatalogForRepo(const std::string& repoUrlOrName)` | Same shape as `getCatalog`, scoped to one repository (identified by URL or name). Parses `m_lib->getCatalogForRepoJson(...)` |
| `downloadPinned` | `LogosMap downloadPinned(const std::string& repoUrlOrName, const std::string& packageName, const std::string& version, const std::string& rootHash)` | Download one exact build. Empty args mean "any": empty repo → any enabled repo, empty version → newest, empty rootHash → don't disambiguate. Returns `{name, path, error?}` (plus `version` / `rootHash` / `repositoryUrl` when those args were supplied) |
| `downloadResolvedDependencies` | `LogosList downloadResolvedDependencies(const std::string& dependenciesJson)` | Resolve a manifest-style dep list (`["name", ...]` or `[{name,version?,signer?}, ...]`) and download every resolved entry in install order. Each result row: `{name, path, error?}`. Exception-fenced to per-package error rows |
| `resolveDependencies` | `LogosList resolveDependencies(const std::string& dependenciesJson, const std::string& installedPackagesJson)` | **Download-free preview.** Resolves the dep list into install-ordered entries `{name, version, rootHash, repositoryUrl, url, topLevel}`. `installedPackagesJson` (optional `[{name,version,rootHash}]`) lets the resolver short-circuit transitive deps already on disk; empty string resolves all transitives from the catalog |
| `catalogChanged` | `void catalogChanged()` *(under `logos_events:`)* | Event signal fired on success from `addRepository` / `removeRepository` / `setRepositoryEnabled`. Subscribers re-fetch via `listRepositories()` / `getCatalog()` |
| `onContextReady` | `void onContextReady() override` *(protected)* | `LogosModuleContext` lifecycle hook. Fires after the host populates `modulePath()` / `instanceId()` / `instancePersistencePath()` and before any method dispatch; re-anchors the lib's `repositories.json` under `instancePersistencePath()` (no-op when that path is empty, keeping the XDG fallback) |

### How it works internally

- **Result shaping.** A private `makeResult(err)` helper builds `{success: err.empty(),
  error?: err}` from the library's "empty string == success" convention. The registry
  mutators (`addRepository` / `removeRepository` / `setEnabled`) and `refreshCatalogs()`
  all follow that convention.
- **Pinned download helper.** A file-local `pinnedDownload(lib, repo, name, version,
  rootHash)` free function does the single-shot `lib->downloadPackage(...)` call and maps
  an empty returned path to a `{name, error}` row, a non-empty path to `{name, path, …}`.
  It is a free function (not a method) so the codegen doesn't see overload ambiguity, and
  it is reused by both `downloadPinned` and `downloadResolvedDependencies`.
- **Exception fence + per-package attribution.** `downloadResolvedDependencies` and
  `resolveDependencies` extract the requested top-level names up front, then run the
  resolver inside a `try/catch`. Any throw (malformed catalog data tripping a
  `nlohmann::json::type_error`, etc.) becomes one `{name, error}` row per requested
  package rather than crashing the whole batch — so a UI that keys install/Failed badges
  by package name always gets a matching update. An unattributed resolver error for a
  single requested package is attributed to that package.
- **Persistence path anchoring.** The constructor seeds `m_lib` with an XDG-style default
  config path (`$XDG_CONFIG_HOME/logos/package-downloader/repositories.json`, falling back
  to `$HOME/.config/...` or a temp dir) so callers that bypass the framework (the `lgpd`
  CLI, unit tests) still get a working downloader. When a host drives the module,
  `onContextReady()` re-points `m_lib` at `<instancePersistencePath>/repositories.json`.
  The replacement is constructed before the old instance is freed, so a throwing ctor
  leaves `m_lib` valid rather than dangling.

## Building and Testing

All builds go through Nix. Prefer the workspace `ws` CLI from the repo root.

```bash
export PATH="/workspace/scripts:$PATH"

# Build (workspace form)
ws build logos-package-downloader-module
ws build logos-package-downloader-module --auto-local   # auto-override dirty deps

# Build (raw nix, from inside the repo)
nix build          # module plugin + auto-generated headers (single combined output)
nix build -L       # verbose (as CI runs it)
```

The build produces, under `result/`:

- `lib/package_downloader_plugin.{so,dylib}` — the Qt plugin
- `lib/libpackage_downloader_lib.{so,dylib}` — the bundled external library
- `include/*.h` — auto-generated client API headers consumed by other modules

### Tests

Unit tests link against a hand-maintained **stub + link-time mock** of
`lgpd::PackageDownloaderLib` / `RepositoryRegistry` instead of the real network/disk
library, so they exercise only the bridge's own logic — JSON pass-through, `{success,
error}` shaping, the pinned-download mapping, and the resolve/download exception fence /
error attribution. No network or disk access.

```bash
# Test (workspace form)
ws test logos-package-downloader-module
ws test logos-package-downloader-module --auto-local

# Test (raw nix, as CI runs it)
nix build .#unit-tests -L
```

The test target is `package_downloader_module_tests`, declared in `tests/CMakeLists.txt`
via the `logos_test()` macro: it compiles `../src/package_downloader_impl.cpp` against the
mock (`mocks/mock_package_downloader_lib.cpp`, registered through `MOCK_C_SOURCES`) and the
stub header (`stubs/package_downloader_lib.h`, on the include path via `EXTRA_INCLUDES`),
together with `main.cpp`, `test_package_downloader.cpp`, and
`package_downloader_events_test.cpp`. The link-time mock is wired by `flake.nix`'s
`tests.mockCLibs = [ "package_downloader" ]`.

Mock default returns when a test leaves a method unconfigured: `"[]"` for the JSON getters
(`listRepositoriesJson` / `getCatalog*Json` / `resolveDependenciesJson`), `""` for the
registry mutators and `refreshCatalogs` (empty == success), and `""` for `downloadPackage`
(empty path == download failure). A test overrides a return with
`t.mockCFunction("<method>").returns("<json-or-error>")`.

#### Test coverage

| Test file | Coverage |
|-----------|----------|
| `test_package_downloader.cpp` | Repo management success/error (`addRepository`, `removeRepository`, `setRepositoryEnabled`), `listRepositories` JSON parsing, `refreshCatalog` success/error, catalog parsing (`getCatalog`, `getCatalogForRepo`), `resolveDependencies` preview (asserts `downloadPackage` is **not** called) + malformed-output attribution, `downloadPinned` success path / failure error-row, and `downloadResolvedDependencies` resolve+download + unnamed-resolver-error attribution + malformed-output per-request error rows |
| `package_downloader_events_test.cpp` | Qt-free body for the `logos_events:` `catalogChanged()` signal, routing it to `logos_test::recordEvent` so event emission can be asserted |
| `main.cpp` | Test entry point: `LOGOS_TEST_MAIN()` from `logos_test.h` |

### Inspecting the built plugin

```bash
lm ./result/lib/package_downloader_plugin.so          # metadata + methods
lm methods ./result/lib/package_downloader_plugin.so --json
```

### Continuous Integration

`.github/workflows/ci.yml` runs on push / PR to `master` or `main`, matrixed over
`ubuntu-latest` + `macos-latest`. It installs Nix (DeterminateSystems installer) and the
`logos-co` Cachix cache, then runs `nix build -L` (module) and `nix build .#unit-tests -L`
(tests).

## Examples

### From another module (type-safe wrappers)

```cpp
LogosModules logos(logosAPI);

// Add a repository, then browse the merged catalog
logos.package_downloader.addRepository("https://example.com/my/logos-repo.json");
QVariantList catalog = logos.package_downloader.getCatalog();

// Preview what installing chat_module would change (no download yet)
QVariantList plan = logos.package_downloader.resolveDependencies(
    R"([{"name":"chat_module"}])", /*installedJson*/"");

// Resolve + download chat_module and its deps, then install each
QVariantList results = logos.package_downloader.downloadResolvedDependencies(
    R"([{"name":"chat_module"}])");
for (const auto& r : results) {
    QVariantMap item = r.toMap();
    if (!item.contains("error"))
        logos.package_manager.installPlugin(item["path"].toString(), false);
}

// Download one exact build (pin repo + version)
QVariantMap one = logos.package_downloader.downloadPinned(
    "my-catalog", "wallet_module", "1.0.0", /*rootHash*/"");
```

### Typical workflows

- **Browse-and-install from a UI.** A caller adds repos (`addRepository`) →
  `refreshCatalog` → `getCatalog` to populate a list; the `catalogChanged` event triggers
  the UI to re-fetch on any repo mutation.
- **Preview-then-commit install.** `resolveDependencies(depsJson, installedJson)` shows the
  install-ordered plan with `topLevel` flags and no download; after user confirmation,
  `downloadResolvedDependencies(depsJson)` downloads each entry and the caller installs the
  returned `.lgx` paths (e.g. via `package_manager.installPlugin`).
- **Pinned / exact download.** `downloadPinned(repo, name, version, rootHash)` fetches a
  single specific build; empty args broaden the match (any repo / newest version / no hash
  disambiguation).
- **Repository administration.** `addRepository` / `removeRepository` /
  `setRepositoryEnabled` mutate the persisted `repositories.json` and emit `catalogChanged`;
  `listRepositories` reports current state including a per-repo `resolveError`.

### Related headless CLI

The same `logos-package-downloader` library is exposed headlessly by the `lgpd` workspace
CLI (browse-then-batch-install without the module):

```bash
lgpd search waku
lgpd download my_module -o ./packages/
lgpm --modules-dir ./modules install --dir ./packages/
```

## Known Limitations

- **Single-line method declarations.** Every method declaration in
  `src/package_downloader_impl.h` MUST be on a single line — the codegen's `--from-header`
  parser scans line-by-line and silently drops methods whose declaration wraps (see
  `repos/logos-cpp-sdk/cpp-generator/experimental/impl_header_parser.cpp`, around
  `if (line.endsWith(';'))`).
- **Legacy single-repo / release-tag shims removed.** `getReleases`,
  `getPackages(tag[, category])`, `getCategories(tag)`, `downloadPackage(tag, name)`,
  `downloadPackages(tag, names)`, and the old `resolveDependencies(tag, names)` were removed
  when the QML UI migrated to the multi-repo API. Only the multi-repo surface remains.
- **No LogosAPI accessor for the module data dir.** The impl synthesises an XDG-style
  default config path in the constructor and re-anchors to the host path in
  `onContextReady()`. When no host provisions persistence (CLI / unit tests) it stays on
  the XDG fallback.
- **Synchronous library calls.** All `lgpd` library calls are synchronous; non-blocking
  behavior depends entirely on the Logos runtime auto-generating `*Async` IPC wrappers.
- **The default repository cannot be removed.** It is hardcoded by the underlying library;
  `removeRepository` on it returns an error.
- **Tests cover bridge logic only.** Unit tests link against a hand-maintained stub
  (`tests/stubs/package_downloader_lib.h`) and mock (`tests/mocks/mock_package_downloader_lib.cpp`)
  covering only the subset of the real `lgpd` surface the impl calls — these must be kept
  in sync with the real library header. They do not exercise real network / disk download.
