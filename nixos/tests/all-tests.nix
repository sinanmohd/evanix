{
  nixos-lib,
  pkgs,
}:

let
  dsl = ./dsl.nix;
  diamond.dag = {
    nodes.a = { };
    nodes.b.inputs.a = { }; # b->a
    nodes.c.inputs.a = { }; # c->a
    nodes.d.inputs.b = { }; # d->b
    nodes.d.inputs.c = { }; # d->c
  };
in
builtins.mapAttrs
  (
    name: value:
    nixos-lib.runTest (
      {
        inherit name;
        hostPkgs = pkgs;
        testScript = ''
          start_all()
          substituter.wait_for_unit("nix-serve.service")
          builder.succeed("dag-test")
        '';
      }
      // value
    )
  )
  {
    diamond-unbuilt-0 = {
      imports = [
        {
          dag.needBuilds = 0;
          dag.needDownloads = 0;
        }
        diamond
        dsl
      ];
    };
    diamond-unbuilt-2 = {
      imports = [
        {
          dag.nodes.a.cache = "remote";
          dag.nodes.b.cache = "remote";
          dag.nodes.d.request = true;
          dag.needBuilds = 2;
          dag.needDownloads = 2;
        }
        diamond
        dsl
      ];
    };
    diamond-unbuilt-4 = {
      imports = [
        {
          dag.nodes.d.request = true;
          dag.needBuilds = 4;
          dag.needDownloads = 0;
        }
        diamond
        dsl
      ];
    };
  }
