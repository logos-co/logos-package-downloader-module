# Builds the logos-package-downloader-module plugin library
{ pkgs, common, src, logosPackageDownloader }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-lib";
  version = common.version;

  inherit src;
  inherit (common) nativeBuildInputs buildInputs cmakeFlags meta env;

  installPhase = ''
    runHook preInstall

    mkdir -p $out/lib
    if [ -f modules/package_downloader_plugin.dylib ]; then
      cp modules/package_downloader_plugin.dylib $out/lib/
    elif [ -f modules/package_downloader_plugin.so ]; then
      cp modules/package_downloader_plugin.so $out/lib/
    else
      echo "Error: No plugin library file found"
      exit 1
    fi

    # Bundle the downloader library alongside the plugin
    for libname in libpackage_downloader_lib; do
      if [ -f ${logosPackageDownloader}/lib/$libname.dylib ]; then
        cp ${logosPackageDownloader}/lib/$libname.dylib $out/lib/
      elif [ -f ${logosPackageDownloader}/lib/$libname.so ]; then
        cp ${logosPackageDownloader}/lib/$libname.so $out/lib/
      fi
    done

    runHook postInstall
  '';
}
