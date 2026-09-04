{
  description = "Logos Package Downloader Module - Online package catalog and download service";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-package-downloader.url = "github:logos-co/logos-package-downloader?rev=4964ebf28dcc207310f0e3a609857d460def65d4";
    storage_module.url = "github:logos-co/logos-storage-module?rev=30ade568b46d66e5aff28d917f079a84388f6a4f";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      externalLibInputs = {
        # Use the `lib` package of logos-package-downloader (it ships
        # libpackage_downloader_lib.{so,dylib,dll} + headers under that
        # output, not under `default`).
        #
        # This mapping is resolved per TARGET, not per name: mkLogosModule
        # reads `input.packages.${system}.lib` for whatever system it is
        # building. So the x86_64-windows target this module advertises is
        # only real because logos-package-downloader itself publishes
        # packages.x86_64-windows.lib -- it has since its own #24, which is
        # the rev this lock already pinned, so enabling Windows here needed
        # no re-pin of it. Pin it back one commit (4f2b684) and the eval
        # fails loudly with
        #   External lib "package_downloader": flake input does not provide
        #   packages.x86_64-windows.lib
        # rather than quietly dropping the target -- so the two pins must
        # not be allowed to drift apart.
        package_downloader = {
          input = inputs.logos-package-downloader;
          packages.default = "lib";
        };
      };
      tests = {
        dir = ./tests;
        # Link-time-mock the package_downloader external lib: the real
        # network/disk-backed PackageDownloaderLib is replaced by
        # tests/mocks/mock_package_downloader_lib.cpp. Key matches the
        # externalLibInputs entry above.
        mockCLibs = [ "package_downloader" ];
      };
    };
}
