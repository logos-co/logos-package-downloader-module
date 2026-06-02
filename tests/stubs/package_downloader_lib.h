#pragma once

// Unit-test stub for the lgpd::PackageDownloaderLib surface that
// PackageDownloaderImpl consumes. Declares only the subset of the real
// `logos-package-downloader` header that the bridge actually calls, so
// the impl compiles and links against the link-time mock
// (mocks/mock_package_downloader_lib.cpp) instead of the real network/
// disk-backed library.
//
// The real header lives in the `logos-package-downloader` repo; this
// stub deliberately omits everything the impl doesn't touch (Fetcher,
// Repository, kDefaultRepositoryUrl, the registry's list/refresh/find
// helpers, etc.). Keep it in sync with the methods invoked in
// src/package_downloader_impl.cpp.

#include <string>

namespace lgpd {

class RepositoryRegistry {
public:
    // All three return an empty string on success or an error message.
    std::string addRepository(const std::string& url);
    std::string removeRepository(const std::string& url);
    std::string setEnabled(const std::string& url, bool enabled);
};

class PackageDownloaderLib {
public:
    PackageDownloaderLib();
    explicit PackageDownloaderLib(std::string configPath);
    ~PackageDownloaderLib();

    PackageDownloaderLib(const PackageDownloaderLib&) = delete;
    PackageDownloaderLib& operator=(const PackageDownloaderLib&) = delete;

    RepositoryRegistry& registry();
    const RepositoryRegistry& registry() const;

    std::string listRepositoriesJson();
    std::string getCatalogJson();
    std::string getCatalogForRepoJson(const std::string& urlOrName);
    std::string refreshCatalogs();

    std::string downloadPackage(const std::string& repoUrlOrName,
                                const std::string& packageName,
                                const std::string& version = "",
                                const std::string& rootHash = "",
                                const std::string& outputDir = "");

    std::string resolveDependenciesJson(const std::string& dependenciesJson,
                                        const std::string& installedPackagesJson = "");

private:
    RepositoryRegistry registry_;
};

} // namespace lgpd
