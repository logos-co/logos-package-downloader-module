{
  description = "Logos Package Downloader Module - Online package catalog and download service";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    logos-package-downloader.url = "github:logos-co/logos-package-downloader";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
      externalLibInputs = {
        # Use the `lib` package of logos-package-downloader (it ships
        # libpackage_downloader_lib.{so,dylib} + headers under that
        # output, not under `default`).
        package_downloader = {
          input = inputs.logos-package-downloader;
          packages.default = "lib";
        };
      };
    };
}
