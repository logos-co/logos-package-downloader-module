#pragma once

#include "interface.h"

class PackageDownloaderInterface : public PluginInterface
{
public:
    virtual ~PackageDownloaderInterface() {}
};

#define PackageDownloaderInterface_iid "org.logos.PackageDownloaderInterface"
Q_DECLARE_INTERFACE(PackageDownloaderInterface, PackageDownloaderInterface_iid)
