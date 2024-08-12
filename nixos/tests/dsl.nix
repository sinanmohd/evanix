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
      options.request = lib.mkEnableOption "Whether to mark the node for building";
      options.assertNeeded = lib.mkOption {
        type = with lib.types; nullOr bool;
        default = null;
        description = "Whether the node must be built to satisfy all requests (either a requested node or a transitive dependency)";
      };
      options.assertChosen = lib.mkOption {
        type = with lib.types; nullOr bool;
        default = null;
        description = "Whether the node is included in the build plan (i.t. it's `needed` and fitted into budget)";
      };
      options.cache = lib.mkOption {
        type = lib.types.enum [
          "none"
          "remote"
          "local"
        ];
        description = ''
          Whether the dependency is pre-built and available in the local /nix/store ("local"), can be substituted ("remote"), or has to be built ("none")
        '';
        default = "none";
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
    needBuilds = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many builds are required to satisfy all targets. Null disables the test";
    };
    needDownloads = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many downloads are required to satisfy all targets. Null disables the test";
    };
    choseBuilds = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many builds we expect evanix to choose to satisfy the maximum number of targets within the given budget. Null disables the test";
    };
    choseDownloads = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many downloads we expect evanix to choose to satisfy the maximum number of targets within the given budget. Null disables the test";
    };
    allowBuilds = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many builds evanix is allowed to choose. Null means no constraint";
    };
    allowDownloads = lib.mkOption {
      type = with lib.types; nullOr int;
      default = null;
      description = "How many downloads evanix is allowed to choose. Null means no constraint";
    };
  };

  config.nodes.builder =
    { pkgs, ... }:
    let
      scope = pkgs.lib.makeScope pkgs.newScope scope-fun;
      configJson = (pkgs.formats.json { }).generate "nix-dag.json" config.dag;
      expressions = pkgs.writeText "guest-scope.nix" ''
        let
          pkgs = import ${pkgs.path} { };
          config = builtins.fromJSON (builtins.readFile ${configJson});
        in
          pkgs.lib.makeScope pkgs.newScope (pkgs.callPackage ${./scope-fun.nix} { inherit (pkgs) lib; inherit (config) nodes; })
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
          p = subprocess.run(["nix-build", "${expressions}", "--dry-run", "--show-trace", "-A", name], check=True, stderr=subprocess.PIPE)
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
          if node["cache"] == "local":
            assert name not in drv_to_action, error_msg
          elif node["cache"] == "remote":
            assert drv_to_action.get(name, None) == "fetch", error_msg
          elif node["cache"] == "unbuilt":
            assert drv_to_action.get(name, None) == "build", error_msg

        need_dls, need_builds = set(), set()
        for name, node in nodes.items():
          if node["request"]:
            need_dls.update(drv_to_schedule[name][0])
            need_builds.update(drv_to_schedule[name][1])

        if (expected_need_dls := config.get("needDownloads", None)) is not None:
          assert len(need_dls) == expected_need_dls, f"{len(need_dls)} != {expected_need_dls}; building {need_dls}"
          print("Verified `needDownloads`", file=sys.stderr)

        if (expected_need_builds := config.get("needBuilds", None)) is not None:
          assert len(need_builds) == expected_need_builds, f"{len(need_builds)} != {expected_need_builds}; building {need_builds}"
          print("Verified `needBuilds`", file=sys.stderr)
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
          expressions
          pkgs.path

          # Cache runCommand's dependencies such as runtimeShell
          (pkgs.runCommand "any-run-command" { } "").inputDerivation
        ];
      networking.hostName = "builder";
      networking.domain = "evanix-tests.local";
      nix.settings.substituters = lib.mkForce [ "http://substituter" ];
      systemd.tmpfiles.settings."10-expressions" = {
        "/run/dag-test/nix-dag-test.json"."L+".argument = "${configJson}";
        "/run/dag-test/scope.nix"."L+".argument = "${expressions}";
      };
      environment.systemPackages = [ tester ];
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
