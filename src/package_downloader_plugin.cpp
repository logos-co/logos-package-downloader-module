#include "package_downloader_plugin.h"
#include <package_downloader_lib.h>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include "logos_api_client.h"

PackageDownloaderPlugin::PackageDownloaderPlugin()
    : m_lib(nullptr)
{
    qDebug() << "PackageDownloaderPlugin created";
    m_lib = new PackageDownloaderLib();
}

PackageDownloaderPlugin::~PackageDownloaderPlugin()
{
    delete m_lib;
    if (logosAPI) {
        delete logosAPI;
        logosAPI = nullptr;
    }
}

QVariantList PackageDownloaderPlugin::getPackages(const QString& releaseTag)
{
    std::string json = m_lib->getPackages(releaseTag.toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).array().toVariantList();
}

QVariantList PackageDownloaderPlugin::getPackages(const QString& releaseTag, const QString& category)
{
    std::string json = m_lib->getPackages(releaseTag.toStdString(), category.toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).array().toVariantList();
}

QStringList PackageDownloaderPlugin::getCategories(const QString& releaseTag)
{
    std::string json = m_lib->getCategories(releaseTag.toStdString());
    QJsonArray arr = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
    QStringList result;
    for (const auto& val : arr) {
        result << val.toString();
    }
    return result;
}

QStringList PackageDownloaderPlugin::resolveDependencies(const QString& releaseTag, const QStringList& packageNames)
{
    std::vector<std::string> names;
    for (const auto& name : packageNames) {
        names.push_back(name.toStdString());
    }

    std::string json = m_lib->resolveDependencies(releaseTag.toStdString(), names);
    QJsonArray arr = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
    QStringList result;
    for (const auto& val : arr) {
        result << val.toString();
    }
    return result;
}

QVariantList PackageDownloaderPlugin::getReleases()
{
    std::string json = m_lib->getReleases();
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).array().toVariantList();
}

QVariantMap PackageDownloaderPlugin::downloadPackage(const QString& releaseTag, const QString& packageName)
{
    qDebug() << "Downloading package:" << packageName << "from release:" << releaseTag;

    std::string filePath = m_lib->downloadPackage(releaseTag.toStdString(), packageName.toStdString());

    QVariantMap result;
    result["name"] = packageName;
    if (filePath.empty()) {
        result["error"] = QStringLiteral("Failed to download package");
    } else {
        result["path"] = QString::fromStdString(filePath);
    }
    return result;
}

QVariantList PackageDownloaderPlugin::downloadPackages(const QString& releaseTag, const QStringList& packageNames)
{
    qDebug() << "Downloading packages:" << packageNames << "from release:" << releaseTag;

    if (packageNames.isEmpty()) {
        return {};
    }

    // Resolve dependencies first
    std::vector<std::string> names;
    for (const auto& name : packageNames) {
        names.push_back(name.toStdString());
    }
    std::string resolvedJson = m_lib->resolveDependencies(releaseTag.toStdString(), names);
    QJsonArray resolvedArr = QJsonDocument::fromJson(QByteArray::fromStdString(resolvedJson)).array();

    QVariantList results;
    for (const auto& val : resolvedArr) {
        results.append(QVariant::fromValue(downloadPackage(releaseTag, val.toString())));
    }
    return results;
}

void PackageDownloaderPlugin::initLogos(LogosAPI* logosAPIInstance)
{
    if (logosAPI) {
        delete logosAPI;
    }
    logosAPI = logosAPIInstance;
}
