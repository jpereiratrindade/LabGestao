{
  description = "LabGestao development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, utils }:
    utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
          ];

          buildInputs = with pkgs; [
            SDL2
            libGL
          ];

          shellHook = ''
            echo "🚀 LabGestao Development Environment"
            echo "Dependencies: SDL2, OpenGL, CMake"
            echo "Usage: cmake -B build && cmake --build build"
          '';
        };
      }
    );
}
