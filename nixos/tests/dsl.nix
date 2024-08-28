{ lib, config, ... }:

let
  Dependency =
    { name, ... }:
    {
      options.name = lib.mkOption {
        type = lib.types.str;
        default = name;
      };
      options.runtime = lib.mkEnableOption "Keep a reference in the output store path to retain a runtime dependency";
    };
  Node = (
    { name, ... }:
    {
      options.name = lib.mkOption {
        type = lib.types.str;
        default = name;
      };
      options.goal = lib.mkEnableOption ''Mark for building (node is a "goal", "target")'';
      options.test.needed = lib.mkOption {
        type = with lib.types; nullOr bool;
        default = null;
        description = "Verify `nix-build --dry-run` reports node as any of to-be built or to-be fetched";
      };
      options.test.chosen = lib.mkOption {
        type = with lib.types; nullOr bool;
        default = null;
        description = "Whether the node is included in the build plan (i.t. it's `needed` and fitted into budget)";
      };
      options.cache = lib.mkOption {
        type = lib.types.enum [
          "unbuilt"
          "remote"
          "local"
        ];
        description = ''
          Whether the dependency is pre-built and available in the local /nix/store ("local"), can be substituted ("remote"), or has to be built ("none")
        '';
        default = "unbuilt";
      };
      options.inputs = lib.mkOption {
        type = lib.types.attrsOf (lib.types.submodule Dependency);
        default = { };
      };
    }
  );
  Nodes = lib.types.attrsOf (lib.types.submodule Node);
  scope-fun = import ./scope-fun.nix {
    inherit lib;
    inherit (config.dag) nodes;
  };
in
{
  options.dag = {
    nodes = lib.mkOption {
      type = Nodes;
      description = "Derivation DAG, including cache status and references.";
    };
    test.unconstrained.builds = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many builds are required to satisfy all targets. Null disables the test";
    };
    test.unconstrained.downloads = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many downloads are required to satisfy all targets. Null disables the test";
    };
    test.constrained.builds = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many builds we expect evanix to choose to satisfy the maximum number of targets within the given budget. Null disables the test";
    };
    test.constrained.downloads = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many downloads we expect evanix to choose to satisfy the maximum number of targets within the given budget. Null disables the test";
    };
    constraints.builds = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many builds evanix is allowed to choose. Null means no constraint";
    };
    constraints.downloads = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many downloads evanix is allowed to choose. Null means no constraint";
    };
  };

  config.nodes.builder =
    { pkgs, ... }:
    let
      evanix = pkgs.callPackage ../../package.nix { };

      scope = pkgs.lib.makeScope pkgs.newScope scope-fun;
      configJson = (pkgs.formats.json { }).generate "nix-dag.json" config.dag;
      allPackages = pkgs.writeText "guest-scope.nix" ''
        let
          pkgs = import ${pkgs.path} { };
          config = builtins.fromJSON (builtins.readFile ${configJson});
        in
          pkgs.lib.makeScope pkgs.newScope (pkgs.callPackage ${./scope-fun.nix} { inherit (pkgs) lib; inherit (config) nodes; })
      '';
      targets = pkgs.writeText "guest-request-scope.nix" ''
        let
          inherit (pkgs) lib;
          pkgs = import ${pkgs.path} { };
          config = builtins.fromJSON (builtins.readFile ${configJson});
          all = import ${allPackages};
          subset = lib.pipe all [
            (lib.filterAttrs (name: node: lib.isDerivation node))
            (lib.filterAttrs (name: node: config.nodes.''${name}.goal))
          ];
        in
          subset
      '';

      tester = pkgs.writers.writePython3Bin "dag-test" { } ''
        # flake8: noqa

        import json
        import re
        import subprocess
        import sys


        with open("${configJson}", "r") as f:
          config = json.load(f)


        nodes = config["nodes"]
        print(f"config={config}", file=sys.stderr)


        def path_to_name(path: str) -> str:
          return re.sub(r"^[ ]*${builtins.storeDir}/[a-z0-9]*-([a-zA-Z0-9_-]+)(\.drv)?", r"\1", path)

        def parse_evanix_dry_run(output):
          to_build = [ ]

          for line in output.split("\n"):
            if not re.match("nix-build --out-link .*$", line):
              continue

            drv = re.sub(r"^nix-build --out-link result-([a-zA-Z0-9_-]+).*$", r"\1", line)
            to_build.append(drv)

          return to_build

        def parse_dry_run(output):
          to_fetch = [ ]
          to_build = [ ]

          bin = "undefined"
          for line in output.split("\n"):

            if not line:
              continue

            if re.match("^.*will be built:$", line):
              bin = "to_build"
              continue
            elif re.match("^.*will be fetched.*:$", line):
              bin = "to_fetch"
              continue

            if not re.match("^[ ]*${builtins.storeDir}", line):
              print(f"Skipping line: {line}", file=sys.stderr)
              continue

            line = path_to_name(line)

            if bin == "to_build":
              to_build.append(line)
            elif bin == "to_fetch":
              to_fetch.append(line)
            else:
              raise RuntimeError("nix-build --dry-run produced invalid output", line)
          return to_fetch, to_build

        drv_to_schedule = {}
        for name, node in nodes.items():
          p = subprocess.run(["nix-build", "${allPackages}", "--dry-run", "--show-trace", "-A", name], check=True, stderr=subprocess.PIPE)
          output = p.stderr.decode("utf-8")
          to_fetch, to_build = parse_dry_run(output)
          drv_to_schedule[name] = (to_fetch, to_build)

        drv_to_action = {}
        for (to_fetch, to_build) in drv_to_schedule.values():
          for dep in to_fetch:
            name = path_to_name(dep)
            if name not in drv_to_action:
              drv_to_action[name] = "fetch"
            assert drv_to_action[name] == "fetch", f"Conflicting plan for {dep}"
          for dep in to_build:
            name = path_to_name(dep)
            if name not in drv_to_action:
              drv_to_action[name] = "build"
            assert drv_to_action[name] == "build", f"Conflicting plan for {dep}"

        print(f"Schedule: {drv_to_action}", file=sys.stderr)
        print(f"Per-derivation schedules: {drv_to_schedule}", file=sys.stderr)

        for name, node in nodes.items():
          error_msg = f"Wrong plan for {name}"
          action = drv_to_action.get(name, None)
          if node["cache"] == "local":
            assert action is None, error_msg
          elif node["cache"] == "remote":
            assert action == "fetch", error_msg
          elif node["cache"] == "unbuilt":
            assert action == "build", error_msg
          else:
            raise AssertionError('cache is not in [ "local", "remote", "unbuilt" ]')

        need_builds: set[str] = set()
        need_dls: set[str] = set()
        for name, node in nodes.items():
          if node["goal"]:
            need_dls.update(drv_to_schedule[name][0])
            need_builds.update(drv_to_schedule[name][1])

        if (expected_need_dls := config["test"]["unconstrained"]["downloads"]) is not None:
          assert len(need_dls) == expected_need_dls, f"{len(need_dls)} != {expected_need_dls}; building {need_dls}"
          print("Verified `needDownloads`", file=sys.stderr)

        if (expected_need_builds := config["test"]["unconstrained"]["builds"]) is not None:
          assert len(need_builds) == expected_need_builds, f"{len(need_builds)} != {expected_need_builds}; building {need_builds}"
          print("Verified `needBuilds`", file=sys.stderr)

        for name, node in nodes.items():
          if node["test"]["needed"]:
            assert name in need_builds or name in need_dls, f"{name}.test.needed violated"


        evanix_args = ["evanix", "${targets}", "--dry-run", "--close-unused-fd", "false"]
        if (allow_builds := config["constraints"]["builds"]) is not None:
          evanix_args.extend(["--solver=highs", "--max-builds", str(allow_builds)])

        expect_chosen_nodes = set(name for name, node in nodes.items() if node["test"]["chosen"])
        expect_n_chosen_builds = config["test"]["constrained"]["builds"]
        expect_n_chosen_downloads = config["test"]["constrained"]["downloads"]

        # TODO: Add option
        if expect_n_chosen_downloads is not None and expect_n_chosen_builds is not None:
          expect_n_chosen_nodes = expect_n_chosen_builds + expect_n_chosen_downloads
        else:
          expect_n_chosen_nodes = None

        if expect_chosen_nodes or expect_n_chosen_builds is not None or expect_n_chosen_downloads is not None:
          evanix = subprocess.run(evanix_args, check=True, stdout=subprocess.PIPE)
          evanix_output = evanix.stdout.decode("utf-8")
          evanix_choices = parse_evanix_dry_run(evanix_output)
        else:
          evanix_choices = set()

        evanix_builds, evanix_downloads = [], []
        for choice in evanix_choices:
          if drv_to_action[choice] == "build":
            evanix_builds.append(choice)
          elif drv_to_action[choice] == "fetch":
            evanix_downloads.append(choice)

        if expect_n_chosen_nodes is not None:
          assert len(evanix_choices) == expect_n_chosen_nodes, f"len({evanix_builds}) != choseNodes"
          print("Verified `choseNodes`", file=sys.stderr)

        if expect_chosen_nodes:
          for name in expect_chosen_nodes:
            assert name in evanix_choices, f"{name}.test.chosen failed; choices: {evanix_choices}"
          print("Verified `expect_chosen_nodes`", file=sys.stderr)

        assert expect_n_chosen_builds is None or len(evanix_builds) == expect_n_chosen_builds, f"{expect_n_chosen_builds=} {len(evanix_builds)=}"
        assert expect_n_chosen_downloads is None or len(evanix_downloads) == expect_n_chosen_downloads, f"{expect_n_chosen_downloads=} {len(evanix_downloads)=}"
      '';
    in
    {
      system.extraDependencies =
        lib.pipe config.dag.nodes [
          builtins.attrValues
          (builtins.filter ({ cache, ... }: cache == "local"))
          (builtins.map ({ name, ... }: scope.${name}))
        ]
        ++ [
          pkgs.path

          # Cache runCommand's dependencies such as runtimeShell
          (pkgs.runCommand "any-run-command" { } "").inputDerivation
        ];
      networking.hostName = "builder";
      networking.domain = "evanix-tests.local";
      nix.settings.substituters = lib.mkForce [ "http://substituter" ];
      systemd.tmpfiles.settings."10-expressions" = {
        "/run/dag-test/nix-dag-test.json"."L+".argument = "${configJson}";
        "/run/dag-test/all-packages.nix"."L+".argument = "${allPackages}";
        "/run/dag-test/targets.nix"."L+".argument = "${targets}";
      };

      environment.systemPackages = [
        tester
        evanix
      ];
    };
  config.nodes.substituter =
    { pkgs, ... }:
    let
      scope = pkgs.lib.makeScope pkgs.newScope scope-fun;
    in
    {
      system.extraDependencies = lib.pipe config.dag.nodes [
        builtins.attrValues
        (builtins.filter ({ cache, ... }: cache == "remote"))
        (builtins.map ({ name, ... }: scope.${name}))
      ];
      services.nix-serve.enable = true;
      services.nix-serve.port = 80;
      services.nix-serve.openFirewall = true;

      # Allow listening on 80
      systemd.services.nix-serve.serviceConfig.User = lib.mkForce "root";
      networking.hostName = "substituter";

      networking.domain = "evanix-tests.local";
    };
}
