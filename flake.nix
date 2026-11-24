{
  description = "NEUROGENIC hardware-independent music workstation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    miniaudio = { url = "github:mackron/miniaudio/0.11.21"; flake = false; };
    libremidi = { url = "github:celtera/libremidi/v5.4.2"; flake = false; };
    slint = { url = "github:slint-ui/slint/v1.19.0"; flake = false; };
    corrosion = { url = "github:corrosion-rs/corrosion/v0.6.1"; flake = false; };
    sol2 = { url = "github:ThePhD/sol2/v3.5.0"; flake = false; };
    lua = { url = "https://www.lua.org/ftp/lua-5.4.8.tar.gz"; flake = false; };
  };

  outputs = inputs@{ self, nixpkgs, miniaudio, libremidi, slint, corrosion,
    sol2, lua, ... }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      sourceRevision =
        if self ? rev && self.rev != null then self.rev
        else if self ? dirtyRev && self.dirtyRev != null then self.dirtyRev
        else "local-path-unversioned";
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      packageFor = pkgs: { runTests, withGuiBackend, withSlint ? false }:
        let
          slintSource = pkgs.applyPatches {
            name = "slint-1.19.0-neurogenic-source";
            src = slint;
            patches = [ ./packaging/slint-no-jemalloc.patch ];
          };
          slintCargoDeps = pkgs.rustPlatform.fetchCargoVendor {
            src = slintSource;
            hash = "sha256-5SAXsaKRxITsGHjE3iZkF10cdAonr6h36BOJDVInW38=";
          };
        in pkgs.stdenv.mkDerivation ({
        pname = "neurogenic";
        version = "0.1.0";
        src = pkgs.lib.cleanSourceWith {
          src = self;
          filter = path: type:
            let name = baseNameOf path;
            in !(builtins.elem name [ "result" "results" ]
              || (type == "directory" && name == "build"));
        };
        nativeBuildInputs = with pkgs; [ cmake ninja pkg-config ]
          ++ pkgs.lib.optionals withSlint [ cargo rustc ];
        buildInputs = with pkgs; [ alsa-lib ]
          ++ pkgs.lib.optional withGuiBackend pkgs.sdl3
          ++ pkgs.lib.optionals withSlint [
            fontconfig freetype libxkbcommon libxkbcommon.dev wayland libGL
            libx11 libxcursor libxi
          ];
        cmakeFlags = [
          "-DNEUROGENIC_FETCH_DEPENDENCIES=ON"
          "-DNEUROGENIC_BUILD_TESTS=${if runTests then "ON" else "OFF"}"
          "-DNEUROGENIC_ENABLE_GUI=${if withGuiBackend then "ON" else "OFF"}"
          "-DNEUROGENIC_ENABLE_SLINT=${if withSlint then "ON" else "OFF"}"
          "-DNEUROGENIC_BUILD_COMMIT=${sourceRevision}"
          "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
          "-DFETCHCONTENT_SOURCE_DIR_MINIAUDIO=${miniaudio}"
          "-DFETCHCONTENT_SOURCE_DIR_LIBREMIDI=${libremidi}"
          "-DFETCHCONTENT_SOURCE_DIR_SLINT=${slintSource}"
          "-DFETCHCONTENT_SOURCE_DIR_CORROSION=${corrosion}"
          "-DFETCHCONTENT_SOURCE_DIR_SOL2=${sol2}"
          "-DFETCHCONTENT_SOURCE_DIR_LUA=${lua}"
        ];
        doCheck = runTests;
        checkPhase = "ctest --output-on-failure";
        postInstall = ''
          rm -rf "$out/include" "$out/lib/cmake" "$out/lib/pkgconfig"
          rm -f "$out/bin/slint-compiler"
        '';
      } // pkgs.lib.optionalAttrs withSlint {
        preConfigure = ''
          export CARGO_HOME="$TMPDIR/neurogenic-cargo-home"
          mkdir -p "$CARGO_HOME"
          substitute ${slintCargoDeps}/.cargo/config.toml \
            "$CARGO_HOME/config.toml" \
            --replace-fail '@vendor@' '${slintCargoDeps}'
          export CARGO_NET_OFFLINE=true
        '';
        doInstallCheck = true;
        installCheckPhase = ''
          test -x "$out/bin/neurogenic"
          test -x "$out/bin/neurogenic-slint"
          test -x "$out/bin/neurogenic-diagnostics"
          test -x "$out/bin/neurogenic-discovery"
          test -x "$out/bin/neurogenic-appliance"
          test -x "$out/bin/neurogenic-preflight"
          test -f "$out/share/neurogenic/appliance/neurogenic-appliance.service"
          "$out/bin/neurogenic" --headless --list-devices > device-enumeration.log
          "$out/bin/neurogenic-diagnostics" --headless --json > diagnostics.json
          grep -q 'neurogenic.hardware-diagnostics.v2' diagnostics.json
          "$out/bin/neurogenic-discovery" --json > discovery.json
          grep -q 'neurogenic.application-discovery.v1' discovery.json
          export HOME="$TMPDIR/home"
          export XDG_CONFIG_HOME="$TMPDIR/config"
          export XDG_DATA_HOME="$TMPDIR/data"
          export XDG_STATE_HOME="$TMPDIR/state"
          export XDG_CACHE_HOME="$TMPDIR/cache"
          mkdir -p "$HOME"
          "$out/bin/neurogenic-preflight" --prepare --quiet
          if DISPLAY= WAYLAND_DISPLAY= "$out/bin/neurogenic" --frontend slint \
              --headless --run-for 0.05 > missing-display.log 2>&1; then
            echo "Slint unexpectedly started without a display" >&2
            exit 1
          fi
          grep -q 'Slint requires an available Wayland or X11 display' \
            missing-display.log
        '';
      });
      nativePackages = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          production = packageFor pkgs {
            runTests = true; withGuiBackend = true; withSlint = true;
          };
          headless = packageFor pkgs {
            runTests = true; withGuiBackend = false;
          };
          sdl = packageFor pkgs {
            runTests = true; withGuiBackend = true;
          };
        });
    in {
      packages = forAllSystems (system: {
        default = nativePackages.${system}.production;
        inherit (nativePackages.${system}) production headless sdl;
      });
      checks = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          build-and-test = nativePackages.${system}.headless;
          production-package = nativePackages.${system}.production;
        } // nixpkgs.lib.optionalAttrs (system == "x86_64-linux") {
          # Compilation-only evidence: this binary is not executed on the x86 host.
          # The renderer-independent GUI model is cross-compiled; native ARM
          # builds enable SDL3. Avoid cross-building SDL3's entire multimedia
          # closure solely for a compilation-only CI check.
          aarch64-cross-compile = packageFor pkgs.pkgsCross.aarch64-multiplatform {
            runTests = false; withGuiBackend = false;
          };
        });
      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${nativePackages.${system}.production}/bin/neurogenic-appliance";
          meta.description = "Run NEUROGENIC";
        };
      });
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            inputsFrom = [ nativePackages.${system}.production ];
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
