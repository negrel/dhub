{
  inputs = {
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    let
      outputsWithoutSystem = { };
      outputsWithSystem = flake-utils.lib.eachDefaultSystem (system:
        let
          pkgs = import nixpkgs { inherit system; };
          lib = pkgs.lib;
          deps = with pkgs; [ libuv basu udev ];
        in {
          devShells = {
            default = pkgs.mkShell rec {
              buildInputs = with pkgs;
                [
                  pkg-config

                  clang-tools
                  valgrind-light
                ] ++ deps;
              LD_LIBRARY_PATH = "${lib.makeLibraryPath buildInputs}";
              DEBUG = 1;
            };
          };
          packages = {
            default = pkgs.stdenv.mkDerivation rec {
              name = "dhub";
              version = "0.1.0";

              src = ./.;

              installPhase = ''
                mkdir -p $out/bin
                cp ./build/dhub $out/bin/dhub
              '';

              nativeBuildInputs = with pkgs; [ clang pkg-config ];
              buildInputs = deps;
              LD_LIBRARY_PATH = "${lib.makeLibraryPath buildInputs}";

              PROJECT_DIR = "./";
            };
          };
        });
    in outputsWithSystem // outputsWithoutSystem;
}
