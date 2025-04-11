# NOTE(@SomeoneSerge): copy-pasted from private notes, piecing together a bunch of automation-related hacks. May diverge from the upstream version. Build as `.#doc.evanix-nixcon24` if still using flakes

{
  lib,
  stdenvNoCC,
  source-code-pro,
  powerline-fonts,
  texliveSmall,
  makeFontsConf,
  python3Packages,
}:
let
  texlive =
    let
      extraPackages = ps: [
        ps.lstfiracode
        ps.xecjk
        ps.fandol
        ps.forest
        ps.newunicodechar
      ];
    in
    texliveSmall.withPackages (
      ps:
      [
        ps.biber
        ps.biblatex
        ps.cm-unicode
        ps.fontawesome5
        ps.latexmk

        ps.minted
        ps.beamer
      ]
      ++ lib.optionals (extraPackages != null) (extraPackages ps)
    );
  extraFontsDirectories = [
    "${source-code-pro}/share/fonts/opentype"
    "${powerline-fonts}/share/fonts/truetype"
  ];
  FONTCONFIG_FILE = makeFontsConf {
    fontDirectories = [ "${texlive}/share/texmf/" ] ++ extraFontsDirectories;
  };
  name = "evanix-nixcon24";
in
stdenvNoCC.mkDerivation {
  inherit name;
  preferLocalBuild = true;
  src =
    let
      fs = lib.fileset;
    in
    fs.toSource {
      root = ./.;
      fileset = ./${name}.tex;
    };
  nativeBuildInputs = [
    texlive
    (with python3Packages; toPythonApplication pygments)
  ];
  inherit FONTCONFIG_FILE;
  buildPhase = ''
    runHook preBuild
    latexmk -xelatex -pdfxelatex="xelatex -shell-escape" "$name".tex |& uniq
    runHook postBuild
  '';
  failureHook = ''
    SPACE="    "
    echo "[ERROR] log file:"
    cat "$name".log | uniq | sed "s/^/$SPACE/" >&2
    exit 1
  '';
  installPhase = ''
    runHook preInstall
    mkdir "$out"
    cp "$name".pdf $out/ || rmdir "$out"
    runHook postInstall
  '';
}
