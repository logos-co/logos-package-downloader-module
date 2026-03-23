{ pkgs, common, src, lib, logosSdk, logosPackageDownloader }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-headers";
  version = common.version;

  inherit src;
  inherit (common) meta;

  nativeBuildInputs = [ logosSdk ];

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p ./generated_headers

    if [ -f "${lib}/lib/package_downloader_plugin.dylib" ]; then
      PLUGIN_FILE="${lib}/lib/package_downloader_plugin.dylib"
    elif [ -f "${lib}/lib/package_downloader_plugin.so" ]; then
      PLUGIN_FILE="${lib}/lib/package_downloader_plugin.so"
    else
      echo "Error: No package_downloader_plugin library file found"
      exit 1
    fi

    if [ "$(uname -s)" = "Darwin" ]; then
      export DYLD_LIBRARY_PATH="${lib}/lib:''${DYLD_LIBRARY_PATH:-}"
    else
      export LD_LIBRARY_PATH="${lib}/lib:''${LD_LIBRARY_PATH:-}"
    fi

    echo "Running logos-cpp-generator on $PLUGIN_FILE"
    logos-cpp-generator "$PLUGIN_FILE" --output-dir ./generated_headers --module-only || {
      echo "Warning: logos-cpp-generator failed"
      touch ./generated_headers/.no-api
    }

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p $out/include

    if [ -d ./generated_headers ] && [ "$(ls -A ./generated_headers 2>/dev/null)" ]; then
      cp -r ./generated_headers/* $out/include/
    else
      echo "# Generated headers from metadata.json" > $out/include/.generated
    fi

    runHook postInstall
  '';
}
