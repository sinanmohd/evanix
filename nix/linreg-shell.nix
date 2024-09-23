# An impure shell for bin/linreg.py
# Lets one reuse the system's cache, handy on an airplane...

{
  nixpkgs ? <nixpkgs>,
  pkgs ? import nixpkgs { },
  lib ? pkgs.lib,
}:

pkgs.mkShell {
  packages = [
    (pkgs.python3.withPackages (
      ps: with ps; [
        tqdm
        scipy
        numpy
        opencv4
        matplotlib
        ipykernel
        notebook
        pandas
      ]
    ))
  ];
  shellHook = ''
  '';
}
