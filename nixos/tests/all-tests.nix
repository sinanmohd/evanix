{ lib, testers }:

let
  dsl = ./dsl.nix;
  diamond.dag = {
    nodes.a = { };
    nodes.b.inputs.a = { }; # b->a
    nodes.c.inputs.a = { }; # c->a
    nodes.d.inputs.b = { }; # d->b
    nodes.d.inputs.c = { }; # d->c
  };

  #   A   B   C     D   E
  #   \   |  /      |   |
  #     U  V        W   X
  sunset.dag = {
    nodes =
      let
        goalDependsOn = inputs: {
          goal = true;
          inputs = lib.genAttrs inputs (_: { });
        };
      in
      {
        a = goalDependsOn [ "u" "v" ];
        b = goalDependsOn [ "u" "v" ];
        c = goalDependsOn [ "u" "v" ];
        d = goalDependsOn [ "w" ];
        e = goalDependsOn [ "x" ];

        u = { };
        v = { };
        w = { };
        x = { };
      };
  };
in
builtins.mapAttrs
  (
    name: value:
    testers.runNixOSTest (
      {
        inherit name;
        testScript = ''
          start_all()
          substituter.wait_for_unit("nix-serve.service")
          builder.succeed("dag-test")
        '';
      }
      // value
      // {
        imports = value.imports ++ [ dsl ];
      }
    )
  )
  {
    diamond-unbuilt-0 = {
      imports = [
        {
          dag.test.unconstrained.builds = 0;
          dag.test.unconstrained.downloads = 0;
        }
        diamond
      ];
    };
    diamond-unbuilt-2 = {
      imports = [
        {
          dag.nodes.a.cache = "remote";
          dag.nodes.b.cache = "remote";
          dag.nodes.d.goal = true;
          dag.test.unconstrained.builds = 2;
          dag.test.unconstrained.downloads = 2;
        }
        diamond
      ];
    };
    diamond-unbuilt-4 = {
      imports = [
        {
          dag.nodes.d.goal = true;
          dag.test.unconstrained.builds = 4;
          dag.test.unconstrained.downloads = 0;
        }
        diamond
      ];
    };

    sunset-unbuilt-9 = {
      imports = [
        {
          dag = {
            test.unconstrained.builds = 9;

            constraints.builds = 5;
            test.constrained.builds = 3;

            nodes = {
              a.test = {
                chosen = true;
                needed = true;
              };
              b.test = {
                chosen = true;
                needed = true;
              };
              c.test = {
                chosen = true;
                needed = true;
              };

              d.test.needed = true;
              e.test.needed = true;
              u.test.needed = true;
              v.test.needed = true;
              w.test.needed = true;
              x.test.needed = true;
            };
          };
        }
        sunset
      ];
    };
  }
