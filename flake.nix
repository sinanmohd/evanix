{
  description = "A Nix build Scheduler";

  inputs.nixpkgs.url = "github:NixOs/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }: let
    lib = nixpkgs.lib;

    forSystem = f: system: f {
      inherit system;
      pkgs = import nixpkgs { inherit system; };
    };

    supportedSystems = lib.platforms.unix;
    forAllSystems = f: lib.genAttrs supportedSystems (forSystem f);
  in {
    devShells = forAllSystems ({ system, pkgs }: {
      default = pkgs.mkShell {
        name = "dev";

        buildInputs = with pkgs; [
          jq
          cjson
          nix-eval-jobs

          pkg-config
          meson
          ninja

          gdb
          ccls
          valgrind
          clang-tools # clang-format
        ];

	shellHook = ''
          export PS1="\033[0;34m[󱄅 ]\033[0m $PS1"
        '';
      };
    });

    packages = forAllSystems ({ system, pkgs }: {
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
          nix-eval-jobs
        ];

        postInstall = ''
          wrapProgram $out/bin/evanix \
              --prefix PATH : ${lib.makeBinPath [ pkgs.nix-eval-jobs ]}
        '';

        meta = {
          homepage = "https://git.sinanmohd.com/evanix";

          license = lib.licenses.gpl3;
          platforms = supportedSystems;
          mainProgram = "evanix";

          maintainers = with lib.maintainers; [ sinanmohd ];
        };
      });

      default = self.packages.${system}.evanix;
    });
  };
}
