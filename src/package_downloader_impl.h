#pragma once

#include <string>
#include <vector>
#include <logos_json.h>

class PackageDownloaderLib;

class PackageDownloaderImpl {
public:
    PackageDownloaderImpl();
    ~PackageDownloaderImpl();

    PackageDownloaderImpl(const PackageDownloaderImpl&) = delete;
    PackageDownloaderImpl& operator=(const PackageDownloaderImpl&) = delete;

    // Package catalog
    // releaseTag: GitHub release tag; empty string resolves to "latest"
    LogosList getPackages(const std::string& releaseTag);
    LogosList getPackages(const std::string& releaseTag, const std::string& category);
    LogosList getCategories(const std::string& releaseTag);
    LogosList resolveDependencies(const std::string& releaseTag, const std::vector<std::string>& packageNames);

    // GitHub releases (returns top 30 most recent, each entry is a LogosMap
    // with keys: tag_name, name, published_at, prerelease, html_url)
    LogosList getReleases();

    // Download (blocking — logos core auto-generates async wrappers)
    // Returns: { "name": "...", "path": "...", "error": "..." }
    LogosMap downloadPackage(const std::string& releaseTag, const std::string& packageName);
    // Returns: [ { "name": "...", "path": "...", "error": "..." }, ... ]
    LogosList downloadPackages(const std::string& releaseTag, const std::vector<std::string>& packageNames);

private:
    PackageDownloaderLib* m_lib;
};
