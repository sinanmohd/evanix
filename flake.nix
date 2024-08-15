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

            inputsFrom = [ self.packages.${system}.evanix ];
            packages = with pkgs; [
              nixfmt-rfc-style
              gdb
              ccls
              valgrind
              clang-tools # clang-format
              flamegraph
              linuxKernel.packages.linux_6_6.perf
              hyperfine
              nix-eval-jobs
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
          evanix = pkgs.callPackage ./package.nix { };

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
      legacyPackages = forAllSystems (
        { pkgs, ... }:
        {
          nixosTests = pkgs.callPackage ./nixos/tests/all-tests.nix { };
        }
      );
      checks = forAllSystems (
        { system, pkgs, ... }:
        let
          inherit (pkgs.lib)
            filterAttrs
            isDerivation
            mapAttrs'
            nameValuePair
            pipe
            ;
        in
        pipe self.legacyPackages.${system}.nixosTests [
          (filterAttrs (_: p: isDerivation p))
          (mapAttrs' (name: nameValuePair "nixosTests-${name}"))
        ]
        // {
          inherit (self.packages.${system}) evanix evanix-py;
        }
      );
    };
}
