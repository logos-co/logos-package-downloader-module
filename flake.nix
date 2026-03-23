{
  description = "Logos Package Downloader Module - Online package catalog and download service";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
    logos-module.url = "github:logos-co/logos-module";
    logos-package-downloader.url = "github:logos-co/logos-package-downloader";
    nix-bundle-dir.url = "github:logos-co/nix-bundle-dir";
    nix-bundle-appimage.url = "github:logos-co/nix-bundle-appimage";
  };

  outputs = { self, nixpkgs, logos-nix, logos-cpp-sdk, logos-module, logos-package-downloader, nix-bundle-dir, nix-bundle-appimage }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        inherit system;
        pkgs = import nixpkgs { inherit system; };
        logosSdk = logos-cpp-sdk.packages.${system}.default;
        logosModule = logos-module.packages.${system}.default;
        logosPackageDownloader = logos-package-downloader.packages.${system}.lib;
        dirBundler = nix-bundle-dir.bundlers.${system}.qtApp;
      });
    in
    {
      packages = forAllSystems ({ pkgs, system, logosSdk, logosModule, logosPackageDownloader, dirBundler }:
        let
          common = import ./nix/default.nix { inherit pkgs logosSdk logosModule logosPackageDownloader; };
          src = ./.;

          lib = import ./nix/lib.nix { inherit pkgs common src logosPackageDownloader; };

          include = import ./nix/include.nix { inherit pkgs common src lib logosSdk logosPackageDownloader; };

          combined = pkgs.symlinkJoin {
            name = "logos-package-downloader-module";
            paths = [ lib include ];
          };
        in
        {
          logos-package-downloader-lib = lib;
          logos-package-downloader-include = include;
          lib = lib;

          default = combined;
        }
      );

      devShells = forAllSystems ({ pkgs, logosSdk, logosModule, logosPackageDownloader }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
            pkgs.qt6.qtremoteobjects
          ];

          shellHook = ''
            export LOGOS_CPP_SDK_ROOT="${logosSdk}"
            export LOGOS_MODULE_ROOT="${logosModule}"
            export LOGOS_PACKAGE_DOWNLOADER_ROOT="${logosPackageDownloader}"
            echo "Logos Package Downloader Module development environment"
          '';
        };
      });
    };
}
