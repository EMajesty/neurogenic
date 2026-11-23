{
  description = "NEUROGENIC hardware-independent music workstation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    miniaudio = { url = "github:mackron/miniaudio/0.11.21"; flake = false; };
    libremidi = { url = "github:celtera/libremidi/v5.4.2"; flake = false; };
    sol2 = { url = "github:ThePhD/sol2/v3.5.0"; flake = false; };
    lua = { url = "https://www.lua.org/ftp/lua-5.4.8.tar.gz"; flake = false; };
  };

  outputs = inputs@{ self, nixpkgs, miniaudio, libremidi, sol2, lua, ... }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      sourceRevision =
        if self ? rev && self.rev != null then self.rev
        else if self ? dirtyRev && self.dirtyRev != null then self.dirtyRev
        else "local-path-unversioned";
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      packageFor = pkgs: runTests: withGuiBackend: pkgs.stdenv.mkDerivation {
        pname = "neurogenic";
        version = "0.1.0";
        src = pkgs.lib.cleanSourceWith {
          src = self;
          filter = path: type:
            let name = baseNameOf path;
            in !(builtins.elem name [ "result" "results" ]
              || (type == "directory" && name == "build"));
        };
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config ];
        buildInputs = with pkgs; [ alsa-lib ]
          ++ pkgs.lib.optional withGuiBackend pkgs.sdl3;
        cmakeFlags = [
          "-DNEUROGENIC_FETCH_DEPENDENCIES=ON"
          "-DNEUROGENIC_BUILD_TESTS=${if runTests then "ON" else "OFF"}"
          "-DNEUROGENIC_ENABLE_GUI=${if withGuiBackend then "ON" else "OFF"}"
          "-DNEUROGENIC_ENABLE_SLINT=OFF"
          "-DNEUROGENIC_BUILD_COMMIT=${sourceRevision}"
          "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          "-DFETCHCONTENT_SOURCE_DIR_MINIAUDIO=${miniaudio}"
          "-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI=${libremidi}"
          "-DFETCHCONTENT_SOURCE_DIR_SOL2=${sol2}"
          "-DFETCHCONTENT_SOURCE_DIR_LUA=${lua}"
        ];
        doCheck = runTests;
        checkPhase = "ctest --output-on-failure";
      };
      nativePackages = forAllSystems (system:
        packageFor (import nixpkgs { inherit system; }) true true);
    in {
      packages = forAllSystems (system: { default = nativePackages.${system}; });
      checks = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          build-and-test = nativePackages.${system};
        } // nixpkgs.lib.optionalAttrs (system == "x86_64-linux") {
          # Compilation-only evidence: this binary is not executed on the x86 host.
          # The renderer-independent GUI model is cross-compiled; native ARM
          # builds enable SDL3. Avoid cross-building SDL3's entire multimedia
          # closure solely for a compilation-only CI check.
          aarch64-cross-compile = packageFor pkgs.pkgsCross.aarch64-multiplatform false false;
        });
      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${nativePackages.${system}}/bin/neurogenic";
          meta.description = "Run NEUROGENIC";
        };
      });
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            inputsFrom = [ nativePackages.${system} ];
            packages = with pkgs; [
              gcc gdb cmake ninja pkg-config git cacert direnv cargo rustc
              fontconfig freetype libxkbcommon wayland libGL libx11 libxcursor
              libxi
            ];
            shellHook = ''
              echo "NEUROGENIC (${system}): cmake --preset dev && cmake --build --preset dev && ctest --preset dev"
            '';
          };
        });
    };
}
