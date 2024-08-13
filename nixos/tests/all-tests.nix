pkgs:

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
        abcInputs = {
          u = { };
          v = { };
        };
      in
      {
        a = {
          request = true;
          inputs = abcInputs;
        };
        b = {
          request = true;
          inputs = abcInputs;
        };
        c = {
          request = true;
          inputs = abcInputs;
        };

        d = {
          request = true;
          inputs.w = { };
        };
        e = {
          request = true;
          inputs.x = { };
        };

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
    pkgs.testers.runNixOSTest (
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
          dag.needBuilds = 0;
          dag.needDownloads = 0;
        }
        diamond
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
      ];
    };

    sunset-unbuilt-0 = {
      imports = [
        {
          # all builds
          dag.needBuilds = 9;
          # all builds allowed
          dag.allowBuilds = 5;
          # chosen builds requested
          dag.choseBuilds = 3;
        }
        sunset
      ];
    };
  }
