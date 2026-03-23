#pragma once

#include <QtCore/QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include "package_downloader_interface.h"
#include "logos_api.h"
#include "logos_api_client.h"

class PackageDownloaderLib;

class PackageDownloaderPlugin : public QObject, public PackageDownloaderInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PackageDownloaderInterface_iid FILE "metadata.json")
    Q_INTERFACES(PackageDownloaderInterface PluginInterface)

public:
    PackageDownloaderPlugin();
    ~PackageDownloaderPlugin();

    // Module identity
    QString name() const override { return "package_downloader"; }
    QString version() const override { return "1.0.0"; }

    // Package catalog
    Q_INVOKABLE QJsonArray getPackages();
    Q_INVOKABLE QJsonArray getPackages(const QString& category);
    Q_INVOKABLE QStringList getCategories();
    Q_INVOKABLE QStringList resolveDependencies(const QStringList& packageNames);

    // Configuration
    Q_INVOKABLE void setRelease(const QString& releaseTag);

    // Download (blocking — logos core auto-generates async wrappers)
    // Returns: { "name": "...", "path": "...", "error": "..." }
    Q_INVOKABLE QJsonObject downloadPackage(const QString& packageName);
    // Returns: [ { "name": "...", "path": "...", "error": "..." }, ... ]
    Q_INVOKABLE QJsonArray downloadPackages(const QStringList& packageNames);

    // LogosAPI
    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    PackageDownloaderLib* m_lib;
};
