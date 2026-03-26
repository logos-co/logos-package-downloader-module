# logos-package-downloader-module

Logos module that wraps [logos-package-downloader](https://github.com/logos-co/logos-package-downloader) as a process-isolated service, exposing the online package catalog and download functionality via LogosAPI.

The underlying `logos-package-downloader` library is plain C++17 with libcurl (no Qt). All library calls are synchronous — the Logos runtime auto-generates async wrappers for IPC, so callers never block.

## Module API

All methods are accessible via LogosAPI from other modules and UI plugins.

### Catalog

| Method | Return | Description |
|--------|--------|-------------|
| `getPackages()` | `QVariantList` | Fetch full online package catalog |
| `getPackages(category)` | `QVariantList` | Fetch packages filtered by category |
| `getCategories()` | `QStringList` | List available categories |
| `resolveDependencies(names)` | `QStringList` | Resolve transitive dependencies for a list of packages |

### Configuration

| Method | Description |
|--------|-------------|
| `setRelease(tag)` | Set GitHub release tag (default: latest) |

### Download

| Method | Return | Description |
|--------|--------|-------------|
| `downloadPackage(name)` | `QVariantMap` | Download a single package. Returns `{name, path, error}` |
| `downloadPackages(names)` | `QVariantList` | Download multiple packages (resolves deps first). Returns array of `{name, path, error}` |

Download methods are synchronous in the library but auto-wrapped as async over IPC by the Logos runtime.

### Usage from another module

```cpp
LogosModules logos(logosAPI);

// Browse the catalog
QStringList categories = logos.package_downloader.getCategories();
QVariantList packages = logos.package_downloader.getPackages("networking");

// Download a single package
QVariantMap result = logos.package_downloader.downloadPackage("waku_module");
if (!result.contains("error")) {
    QString filePath = result["path"].toString();
    // Hand off to package_manager for installation
    logos.package_manager.installPlugin(filePath, false);
}

// Download multiple packages (auto-resolves dependencies)
QVariantList results = logos.package_downloader.downloadPackages({"waku_module", "chat_module"});
for (const auto& r : results) {
    QVariantMap item = r.toMap();
    if (!item.contains("error")) {
        logos.package_manager.installPlugin(item["path"].toString(), false);
    }
}
```

## Building

```bash
nix build          # module plugin (lib + include)
nix build .#lib    # plugin .so/.dylib only
```

## Dependencies

- `logos-cpp-sdk` — SDK, LogosAPI, IPC
- `logos-module` — plugin introspection
- `logos-package-downloader` — download library (plain C++ + libcurl, linked at build time)
