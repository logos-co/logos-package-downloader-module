# logos-package-downloader-module

Logos module that wraps [logos-package-downloader](https://github.com/logos-co/logos-package-downloader) as a process-isolated service, exposing the online package catalog and download functionality via LogosAPI.

The underlying `logos-package-downloader` library is plain C++17 with libcurl (no Qt). The module's own implementation (`PackageDownloaderImpl`) is also pure C++ — it lives in `src/package_downloader_impl.{h,cpp}` and uses `std::string` / `std::vector` / `LogosList` (alias for `nlohmann::json`) instead of Qt types. The Qt plugin glue layer is auto-generated at build time by `logos-cpp-generator --from-header`, driven by `"interface": "universal"` in `metadata.json`. All library calls are synchronous — the Logos runtime auto-generates async wrappers for IPC, so callers never block.

## Module API

All methods are accessible via LogosAPI from other modules and UI plugins. The first argument of every catalog/download method is `releaseTag` — the GitHub release tag to query (pass an empty string to resolve to "latest").

### Catalog

| Method | Return | Description |
|--------|--------|-------------|
| `getPackages(releaseTag)` | `QVariantList` | Fetch full online package catalog for the given release |
| `getPackages(releaseTag, category)` | `QVariantList` | Fetch packages filtered by category |
| `getCategories(releaseTag)` | `QVariantList<QString>` | List available categories |
| `resolveDependencies(releaseTag, names)` | `QVariantList<QString>` | Resolve transitive dependencies for a list of packages |
| `getReleases()` | `QVariantList` | Fetch the 30 most recent GitHub releases. Each entry is `{tag_name, name, published_at, prerelease, html_url}` |

### Download

| Method | Return | Description |
|--------|--------|-------------|
| `downloadPackage(releaseTag, name)` | `QVariantMap` | Download a single package. Returns `{name, path, error}` |
| `downloadPackages(releaseTag, names)` | `QVariantList` | Download multiple packages (resolves deps first). Returns array of `{name, path, error}` |

Download methods are synchronous in the library but auto-wrapped as async over IPC by the Logos runtime.

### Usage from another module

```cpp
LogosModules logos(logosAPI);

// Browse the catalog (empty tag → "latest")
QVariantList categories = logos.package_downloader.getCategories("");
QVariantList packages = logos.package_downloader.getPackages("", "networking");

// Download a single package
QVariantMap result = logos.package_downloader.downloadPackage("", "waku_module");
if (!result.contains("error")) {
    QString filePath = result["path"].toString();
    // Hand off to package_manager for installation
    logos.package_manager.installPlugin(filePath, false);
}

// Download multiple packages (auto-resolves dependencies)
QVariantList results = logos.package_downloader.downloadPackages("", {"waku_module", "chat_module"});
for (const auto& r : results) {
    QVariantMap item = r.toMap();
    if (!item.contains("error")) {
        logos.package_manager.installPlugin(item["path"].toString(), false);
    }
}

// Pin a specific release
QVariantList stable = logos.package_downloader.getPackages("v2.0.0");
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
