{ lib, nodes }:
assert builtins.isAttrs nodes;
self:
let
  mkBuildInputs =
    propagated:
    lib.flip lib.pipe [
      builtins.attrValues
      (builtins.filter ({ runtime, ... }: (propagated && runtime) || (!propagated && !runtime)))
      (map ({ name, ... }: self.${name}))
    ];
in
builtins.mapAttrs (
  name: node:
  assert builtins.isString name;
  assert builtins.isAttrs node;
  let
    inherit (node) inputs;
  in
  self.callPackage (
    { runCommand }:
    runCommand name
      {
        buildInputs = mkBuildInputs false inputs;
        propagatedBuildInputs = mkBuildInputs true inputs;
      }
      ''
        mkdir $out 
        echo ${name} > $out/name
      ''
  ) { }
) nodes
