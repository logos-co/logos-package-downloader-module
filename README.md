# logos-package-downloader-module

Logos module that wraps [logos-package-downloader](https://github.com/logos-co/logos-package-downloader) as a process-isolated service, exposing its **multi-repository** catalog + download functionality via LogosAPI.

> 📖 The `logos-repo.json` / `index.json` formats behind this API are
> specified in
> [logos-modules-release-tool/docs/catalog-format.md](https://github.com/logos-co/logos-modules-release-tool/blob/main/docs/catalog-format.md).

The underlying `logos-package-downloader` library is plain C++17 with libcurl (no Qt). The module's own implementation (`PackageDownloaderImpl`) is also pure C++ — it lives in `src/package_downloader_impl.{h,cpp}` and uses `std::string` / `std::vector` / `LogosList` (alias for `nlohmann::json`) instead of Qt types. The Qt plugin glue layer is auto-generated at build time by `logos-cpp-generator --from-header`, driven by `"interface": "universal"` in `metadata.json`. All library calls are synchronous — the Logos runtime auto-generates async wrappers for IPC, so callers never block.

## Module API

All methods are accessible via LogosAPI from other modules and UI
plugins. They're synchronous in the library; the Logos runtime
auto-generates `*Async` wrappers for IPC, so callers never block.

### Repositories

A repository is the URL of a `logos-repo.json`. The built-in default repo
is always present; user repos are added/removed/toggled here and persisted
under the module's data directory. Mutating calls return
`{ success: bool, error?: string }`.

| Method | Return | Description |
|--------|--------|-------------|
| `addRepository(url)` | `QVariantMap` | Add a user repository by its `logos-repo.json` URL |
| `removeRepository(url)` | `QVariantMap` | Remove a user repository (the default can't be removed) |
| `setRepositoryEnabled(url, enabled)` | `QVariantMap` | Enable / disable a repository |
| `listRepositories()` | `QVariantList` | Configured repos: `{url, enabled, isDefault, name, displayName, description, homepage, indexUrl, trustedSignerDids[], resolveError}` |
| `refreshCatalog()` | `QVariantMap` | Re-fetch every enabled repo's `logos-repo.json` + `index.json` |

### Catalog

| Method | Return | Description |
|--------|--------|-------------|
| `getCatalog()` | `QVariantList` | Merged catalog across all enabled repos. Each entry: `{repositoryUrl, repositoryName, repositoryDisplayName, name, type, category, author, description, icon, versions[]}` (versions newest-first) |
| `getCatalogForRepo(urlOrName)` | `QVariantList` | Same shape, scoped to one repository |

### Resolve & download

| Method | Return | Description |
|--------|--------|-------------|
| `resolveDependencies(depsJson, installedJson)` | `QVariantList` | **Preview, no download.** Resolve a manifest-style dep list into install-ordered entries `{name, version, rootHash, repositoryUrl, url, topLevel}`. `installedJson` (optional `[{name,version,rootHash}]`) lets it omit transitive deps already satisfied on-disk |
| `downloadResolvedDependencies(depsJson, installedJson)` | `QVariantList` | Resolve **and** download every entry in install order. Each result: `{name, path, error?}` |
| `downloadPinned(repoUrlOrName, name, version, rootHash)` | `QVariantMap` | Download one exact build. Empty args mean "any": empty repo → any enabled repo, empty version → newest, empty rootHash → don't disambiguate. Returns `{name, path, error?}` |

### Usage from another module

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
    R"([{"name":"chat_module"}])", /*installedPackagesJson*/"");
for (const auto& r : results) {
    QVariantMap item = r.toMap();
    if (!item.contains("error"))
        logos.package_manager.installPlugin(item["path"].toString(), false);
}

// Download one exact build (pin repo + version)
QVariantMap one = logos.package_downloader.downloadPinned(
    "my-catalog", "wallet_module", "1.0.0", /*rootHash*/"");
```

## Building

```bash
nix build          # module plugin + auto-generated headers (single combined output)
ws build logos-package-downloader-module --auto-local   # workspace-aware build with local overrides
```

The build produces `lib/package_downloader_plugin.{so,dylib}` (the Qt plugin) plus `lib/libpackage_downloader_lib.{so,dylib}` (the bundled external library) and `include/*.h` (auto-generated client API headers consumed by other modules).

## Dependencies

Direct flake inputs (`flake.nix`):

- `logos-module-builder` — provides `mkLogosModule`, the Qt plugin glue generator, and brings in `logos-cpp-sdk` + `logos-module` transitively.
- `logos-package-downloader` — the underlying C++ download library (plain C++ + libcurl), staged into `lib/` at build time and linked into the plugin.

Resolved transitively through the builder:

- `logos-cpp-sdk` — LogosAPI, IPC, `LogosMap`/`LogosList` aliases, `logos-cpp-generator`.
- `logos-module` — plugin introspection (`PluginInterface`).
