#include "package_downloader_plugin.h"
#include <package_downloader_lib.h>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
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

QJsonArray PackageDownloaderPlugin::getPackages()
{
    std::string json = m_lib->getPackages();
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
}

QJsonArray PackageDownloaderPlugin::getPackages(const QString& category)
{
    std::string json = m_lib->getPackages(category.toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
}

QStringList PackageDownloaderPlugin::getCategories()
{
    std::string json = m_lib->getCategories();
    QJsonArray arr = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
    QStringList result;
    for (const auto& val : arr) {
        result << val.toString();
    }
    return result;
}

QStringList PackageDownloaderPlugin::resolveDependencies(const QStringList& packageNames)
{
    std::vector<std::string> names;
    for (const auto& name : packageNames) {
        names.push_back(name.toStdString());
    }

    std::string json = m_lib->resolveDependencies(names);
    QJsonArray arr = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
    QStringList result;
    for (const auto& val : arr) {
        result << val.toString();
    }
    return result;
}

void PackageDownloaderPlugin::setRelease(const QString& releaseTag)
{
    m_lib->setRelease(releaseTag.toStdString());
}

QJsonObject PackageDownloaderPlugin::downloadPackage(const QString& packageName)
{
    qDebug() << "Downloading package:" << packageName;

    std::string filePath = m_lib->downloadPackage(packageName.toStdString());

    QJsonObject result;
    result["name"] = packageName;
    if (filePath.empty()) {
        result["error"] = QStringLiteral("Failed to download package");
    } else {
        result["path"] = QString::fromStdString(filePath);
    }
    return result;
}

QJsonArray PackageDownloaderPlugin::downloadPackages(const QStringList& packageNames)
{
    qDebug() << "Downloading packages:" << packageNames;

    if (packageNames.isEmpty()) {
        return {};
    }

    // Resolve dependencies first
    std::vector<std::string> names;
    for (const auto& name : packageNames) {
        names.push_back(name.toStdString());
    }
    std::string resolvedJson = m_lib->resolveDependencies(names);
    QJsonArray resolvedArr = QJsonDocument::fromJson(QByteArray::fromStdString(resolvedJson)).array();

    QJsonArray results;
    for (const auto& val : resolvedArr) {
        results.append(downloadPackage(val.toString()));
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
