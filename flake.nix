{
  description = "A Nix build Scheduler";

  inputs.nixpkgs.url = "github:NixOs/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      lib = nixpkgs.lib;

      forSystem =
        f: system:
        f {
          inherit system;
          pkgs = import nixpkgs { inherit system; };
        };

      supportedSystems = lib.systems.flakeExposed;
      forAllSystems = f: lib.genAttrs supportedSystems (forSystem f);
    in
    {
      devShells = forAllSystems (
        { system, pkgs }:
        {
          default = pkgs.mkShell {
            name = "dev";

            buildInputs = with pkgs; [
              jq
              highs
              cjson
              nix-eval-jobs

              pkg-config
              meson
              ninja

              gdb
              ccls
              valgrind
              clang-tools # clang-format
              flamegraph
              linuxKernel.packages.linux_6_6.perf
            ];

            shellHook = ''
              export PS1="\033[0;34m[󱄅 ]\033[0m $PS1"
            '';
          };
        }
      );

      packages = forAllSystems (
        { system, pkgs }:
        {
          default = self.packages.${system}.evanix;
          evanix = pkgs.stdenv.mkDerivation (finalAttrs: {
            name = "evanix";

            src = ./.;
            nativeBuildInputs = with pkgs; [
              meson
              ninja
              pkg-config
              makeWrapper
            ];
            buildInputs = with pkgs; [
              cjson
              highs
            ];

            mesonFlags = [
              (lib.mesonOption "NIX_EVAL_JOBS_PATH" (lib.getExe pkgs.nix-eval-jobs))
            ];

            meta = {
              homepage = "https://git.sinanmohd.com/evanix";

              license = lib.licenses.gpl3;
              platforms = supportedSystems;
              mainProgram = "evanix";

              maintainers = with lib.maintainers; [ sinanmohd ];
            };
          });

          evanix-py = pkgs.python3Packages.callPackage ./python-package.nix { };
          pythonWithEvanix =
            let
              wrapper = pkgs.python3.withPackages (ps: [ (ps.callPackage ./python-package.nix { }) ]);
            in
            wrapper.overrideAttrs (oldAttrs: {
              makeWrapperArgs = oldAttrs.makeWrapperArgs or [ ] ++ [
                "--prefix"
                "PATH"
                ":"
                "${lib.makeBinPath [ pkgs.nix-eval-jobs ]}"
              ];
            });
        }
      );
    };
}
