Synthetic integration tests for "real" nix stores and substituters

Usage
---

```console
$ nix build .#nixosTests.diamond-unbuilt-2
```

Development
---

The `.#nixosTests` attrset is defined in [`all-tests.nix`](./all-tests.nix).
In [dsl.nix](./dsl.nix) we define the helper for generating NixOS tests from DAGs.
