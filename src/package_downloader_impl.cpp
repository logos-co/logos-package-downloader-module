#include "package_downloader_impl.h"
#include <package_downloader_lib.h>

PackageDownloaderImpl::PackageDownloaderImpl()
    : m_lib(new PackageDownloaderLib())
{
}

PackageDownloaderImpl::~PackageDownloaderImpl()
{
    delete m_lib;
}

LogosList PackageDownloaderImpl::getPackages(const std::string& releaseTag)
{
    std::string json = m_lib->getPackages(releaseTag);
    return LogosList::parse(json);
}

LogosList PackageDownloaderImpl::getPackages(const std::string& releaseTag, const std::string& category)
{
    std::string json = m_lib->getPackages(releaseTag, category);
    return LogosList::parse(json);
}

LogosList PackageDownloaderImpl::getCategories(const std::string& releaseTag)
{
    std::string json = m_lib->getCategories(releaseTag);
    return LogosList::parse(json);
}

LogosList PackageDownloaderImpl::resolveDependencies(const std::string& releaseTag, const std::vector<std::string>& packageNames)
{
    std::string json = m_lib->resolveDependencies(releaseTag, packageNames);
    return LogosList::parse(json);
}

LogosList PackageDownloaderImpl::getReleases()
{
    std::string json = m_lib->getReleases();
    return LogosList::parse(json);
}

LogosMap PackageDownloaderImpl::downloadPackage(const std::string& releaseTag, const std::string& packageName)
{
    std::string filePath = m_lib->downloadPackage(releaseTag, packageName);

    LogosMap result = LogosMap::object();
    result["name"] = packageName;
    if (filePath.empty()) {
        result["error"] = "Failed to download package";
    } else {
        result["path"] = filePath;
    }
    return result;
}

LogosList PackageDownloaderImpl::downloadPackages(const std::string& releaseTag, const std::vector<std::string>& packageNames)
{
    if (packageNames.empty()) {
        return LogosList::array();
    }

    // Resolve dependencies first
    std::string resolvedJson = m_lib->resolveDependencies(releaseTag, packageNames);
    LogosList resolved = LogosList::parse(resolvedJson);

    LogosList results = LogosList::array();
    for (const auto& val : resolved) {
        if (val.is_string()) {
            results.push_back(downloadPackage(releaseTag, val.get<std::string>()));
        } else {
            // resolveDependencies is contracted to return a JSON array of
            // package-name strings. Anything else is a contract violation
            // (lib bug, schema drift, embedded error object). Surface it as
            // an error entry rather than silently dropping it — the old
            // Qt plugin called .toString() on every entry which produced a
            // "Failed to download package" entry with empty name for
            // non-string values; this preserves the "one result per
            // resolved entry" shape callers rely on while making the
            // diagnostic actionable.
            LogosMap entry = LogosMap::object();
            entry["name"]  = "";
            entry["error"] = "Unexpected non-string entry in resolveDependencies result: " + val.dump();
            results.push_back(entry);
        }
    }
    return results;
}
