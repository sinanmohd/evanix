# evanix
Evanix is a Nix build scheduler designed to maximize throughput given limited resources. The input is a graph G=(V,E), where each vertex v represents a Nix package with a weight w(v) and a profit p(v). The profit for a transitive dependency is 0, while it is 1 for packages that we intend to build. Our objective is to identify a set of vertices that maximizes the profit or the number of packages built. It is crucial that a vertex or package can only be selected if all of its dependencies are also selected. An edge in the graph represents a dependency, with the indegree of a vertex denoting the number of packages that depend on it and the outdegree representing the number of dependencies of the package.

# todo

- [x] Utilize mixed-integer programming to approximate the most optimal solution efficiently, using the HiGHS solver.
- [x] Maximize the number of requested packages produced within a specified budget, with a maximum constraint of n builds.
- [x] Maximize the number of requested packages produced within a specified budget, constrained by a maximum time limit of t.
- [x] Utilize data from Hydra to estimate the build times for derivations.
- [x] Develop a linear regression model to estimate build times for derivations when such data is unavailable in Hydra.
- [x] Implement unit tests using Meson’s built-in testing framework, and integration tests utilizing the NixOS integration testing framework.
- [x] Implement a pipelined architecture to initiate the building of derivations immediately following their evaluation.
- [ ] Enhance the linear regression model and conduct additional tests to validate its performance and accuracy.
- [ ] Integrate the linear regression model with evanix
- [ ] 1.0 refactor

# options

```console
$ nix run github:sinanmohd/evanix -- --help
Usage: evanix [options] expr

  -h, --help                         Show help message and quit.
  -f, --flake                        Build a flake.
  -d, --dry-run                      Show what derivations would be built.
  -s, --system                       System to build for.
  -m, --max-builds                   Max number of builds.
  -t, --max-time                     Max time available in seconds.
  -b, --break-evanix                 Enable experimental features.
  -r, --solver-report                Print solver report.
  -p, --pipelined            <bool>  Use evanix build pipeline.
  -l, --check_cache-status   <bool>  Perform cache locality check.
  -c, --close-unused-fd      <bool>  Close stderr on exec.
  -e, --statistics           <path>  Path to time statistics database.
  -k, --solver sjf|conformity|highs  Solver to use.
```
