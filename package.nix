{
  lib,
  cjson,
  highs,
  makeWrapper,
  meson,
  ninja,
  nix-eval-jobs,
  pkg-config,
  stdenv,
  uthash,
  sqlite,
  nix,
  curl,
}:
stdenv.mkDerivation (finalAttrs: {
  name = "evanix";

  src =
    let
      fs = lib.fileset;
    in
    fs.toSource {
      root = ./.;
      fileset = fs.unions [
        ./src
        ./tests
        ./include
        ./meson.build
        ./meson_options.txt
      ];
    };
  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    makeWrapper
  ];
  buildInputs = [
    curl
    nix
    cjson
    highs
    uthash
    sqlite
  ];

  doCheck = true;

  mesonFlags = [ (lib.mesonOption "NIX_EVAL_JOBS_PATH" (lib.getExe nix-eval-jobs)) ];

  meta = {
    homepage = "https://git.sinanmohd.com/evanix";

    license = lib.licenses.gpl3;
    platforms = lib.platforms.all;
    mainProgram = "evanix";

    maintainers = with lib.maintainers; [ sinanmohd ];
  };
})
