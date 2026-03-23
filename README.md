# logos-package-downloader-module

Logos module that wraps [logos-package-downloader](https://github.com/logos-co/logos-package-downloader) as a process-isolated service, exposing the online package catalog and download functionality via LogosAPI.

## Module API

All methods are accessible via LogosAPI from other modules and UI plugins.

| Method | Return | Description |
|--------|--------|-------------|
| `getPackages()` | `QJsonArray` | Fetch full online package catalog |
| `getPackages(category)` | `QJsonArray` | Fetch packages filtered by category |
| `getCategories()` | `QStringList` | List available categories |
| `resolveDependencies(names)` | `QStringList` | Resolve transitive dependencies |
| `setRelease(tag)` | `void` | Set GitHub release tag |
| `downloadPackageAsync(name)` | `void` | Download a single package (async) |
| `downloadPackagesAsync(names)` | `void` | Download multiple packages (async, resolves deps) |

### Events

| Event | Data | Description |
|-------|------|-------------|
| `packageDownloadFinished` | `[packageName, filePath, success, error]` | Emitted when an async download completes |

### Usage from another module

```cpp
LogosModules logos(logosAPI);

// Browse the catalog
QStringList categories = logos.package_downloader.getCategories();
QJsonArray packages = logos.package_downloader.getPackages("networking");

// Download (triggers packageDownloadFinished event)
logos.package_downloader.downloadPackagesAsync({"waku_module", "chat_module"});

// Listen for completion
logos.package_downloader.on("packageDownloadFinished", [](const QVariantList& data) {
    QString name = data[0].toString();
    QString path = data[1].toString();
    bool ok = data[2].toBool();
    // ... hand off to package_manager for installation
});
```

## Building

```bash
nix build          # module plugin (lib + include)
nix build .#lib    # plugin .so/.dylib only
```

## Dependencies

- `logos-cpp-sdk` — SDK, LogosAPI, IPC
- `logos-module` — plugin introspection
- `logos-package-downloader` — download library (linked at build time)
