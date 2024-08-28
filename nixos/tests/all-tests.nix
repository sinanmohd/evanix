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

  #  A
  #  |
  #  B
  #  |
  #  C
  transitive.dag = {
    nodes.a = {
      goal = true;
      inputs.b = { };
    };
    nodes.b.inputs.c = { };
    nodes.c = {};
  };

  #   A   B   C     D
  #   \   |  /      |
  #     U  V        W
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

        u = { };
        v = { };
        w = { };
      };
  };
in
builtins.mapAttrs
  (
    name: value:
    testers.runNixOSTest (
      {
        inherit name;
        imports = value.imports ++ [ dsl ];
        testScript =
          value.testScriptPre or ""
          + ''
            start_all()
            substituter.wait_for_unit("nix-serve.service")
            builder.succeed("dag-test")
          ''
          + value.testScriptPost or "";
      }
      // builtins.removeAttrs value [
        "imports"
        "testScriptPre"
        "testScriptPost"
      ]
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

    transitive-unbuilt-3 = {
      imports = [
        {
          dag = {
            test.unconstrained.builds = 3;

            constraints.builds = 2;
            test.constrained.builds = 0;

            nodes = {
              a.test.needed = true;
              b.test.needed = true;
              c.test.needed = true;
            };
          };
        }
        transitive
      ];
    };

    sunset-unbuilt-7 = {
      imports = [
        {
          dag = {
            test.unconstrained.builds = 7;

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
              u.test.needed = true;
              v.test.needed = true;
              w.test.needed = true;
            };
          };
        }
        sunset
      ];
    };
  }
