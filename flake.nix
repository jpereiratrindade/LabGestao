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
        runtimeLibs = with pkgs; [
          SDL2
          libGL
          mesa
          mesa.drivers
          xorg.libX11
          xorg.libXext
          xorg.libXcursor
          xorg.libXrandr
          xorg.libXinerama
          xorg.libXi
          libxkbcommon
          wayland
        ];
      in
      {
        devShells.default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
          ];

          buildInputs = runtimeLibs ++ [
            pkgs.glibcLocales
            pkgs.dejavu_fonts
          ];

          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath runtimeLibs;
          LIBGL_DRIVERS_PATH = "${pkgs.mesa.drivers}/lib/dri";
          LANG = "C.UTF-8";
          LC_ALL = "C.UTF-8";
          LOCALE_ARCHIVE = "${pkgs.glibcLocales}/lib/locale/locale-archive";
          LABGESTAO_BASE_FONT = "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf";

          shellHook = ''
            echo "🚀 LabGestao Development Environment"
            echo "Dependencies: SDL2, OpenGL, CMake"
            echo "Usage: cmake -B build && cmake --build build"
          '';
        };
      }
    );
}
