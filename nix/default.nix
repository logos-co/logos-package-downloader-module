# Common build configuration shared across all packages
{ pkgs, logosSdk, logosModule, logosPackageDownloader }:

{
  pname = "logos-package-downloader-module";
  version = "1.0.0";

  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
    pkgs.qt6.wrapQtAppsNoGuiHook
  ];

  buildInputs = [
    pkgs.qt6.qtbase
    pkgs.qt6.qtremoteobjects
    logosPackageDownloader
  ];

  cmakeFlags = [
    "-GNinja"
    "-DLOGOS_CPP_SDK_ROOT=${logosSdk}"
    "-DLOGOS_MODULE_ROOT=${logosModule}"
    "-DLOGOS_PACKAGE_DOWNLOADER_ROOT=${logosPackageDownloader}"
    "-DLOGOS_PACKAGE_DOWNLOADER_USE_VENDOR=OFF"
  ];

  env = {
    LOGOS_CPP_SDK_ROOT = "${logosSdk}";
    LOGOS_MODULE_ROOT = "${logosModule}";
    LOGOS_PACKAGE_DOWNLOADER_ROOT = "${logosPackageDownloader}";
  };

  meta = with pkgs.lib; {
    description = "Logos Package Downloader Module - Online package catalog and download service";
    platforms = platforms.unix;
  };
}
