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
}:
stdenv.mkDerivation (finalAttrs: {
  name = "evanix";

  src = ./.;
  nativeBuildInputs = [
    uthash
    meson
    ninja
    pkg-config
    makeWrapper
  ];
  buildInputs = [
    cjson
    highs
  ];

  mesonFlags = [ (lib.mesonOption "NIX_EVAL_JOBS_PATH" (lib.getExe nix-eval-jobs)) ];

  meta = {
    homepage = "https://git.sinanmohd.com/evanix";

    license = lib.licenses.gpl3;
    platforms = lib.platforms.all;
    mainProgram = "evanix";

    maintainers = with lib.maintainers; [ sinanmohd ];
  };
})
