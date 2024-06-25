{
  lib,
  buildPythonPackage,
  meson-python,
  ninja,
  pkg-config,
  makeWrapper,
  cjson,
}:

buildPythonPackage {
  pname = "evanix";
  version = "0.0.1";
  pyproject = true;

  src =
    let
      fs = lib.fileset;
    in
    fs.toSource {
      root = ./.;
      fileset = fs.unions [
        ./src
        ./include
        ./meson.build
        ./meson_options.txt
        ./pyproject.toml
      ];
    };

  build-system = [ meson-python ];
  nativeBuildInputs = [
    ninja
    pkg-config
    makeWrapper
  ];
  buildInputs = [
    cjson
    # nix-eval-jobs
  ];
}
