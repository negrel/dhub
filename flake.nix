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
        in {
          devShells = {
            default = pkgs.mkShell rec {
              buildInputs = with pkgs; [
                pkg-config
                libuv
                basu

                clang-tools
                indent
              ];
              LD_LIBRARY_PATH = "${lib.makeLibraryPath buildInputs}";
              DEBUG = 1;
            };
          };
        });
    in outputsWithSystem // outputsWithoutSystem;
}
