#!/usr/bin/env python3

# import pandas as pd
import sqlite3
from argparse import ArgumentParser
from contextlib import ExitStack
from pathlib import Path
import gzip

import numpy as np
import scipy as sp

parser = ArgumentParser("evanix-linreg")
parser.add_argument("--input-dir", default=Path("data/"))
parser.add_argument("--builds", default=None, help="data/builds_drv_and_time.csv")
parser.add_argument("--drvs", default=None, help="data/drvs.csv")
parser.add_argument("--depends_on", default=None, help="data/depends_on.csv")
parser.add_argument("--pnames", default=None, help="data/pnames.csv")
parser.add_argument("--index", default=None, help="data/hydra.db")


def load_csv(path, dtype: np.dtype = np.dtype("int32"), *args, **kwargs):
    return np.loadtxt(path, dtype=dtype, delimiter=",", *args, **kwargs)


def read_dag(depends_on_path, n_drvs, n_pnames, max_size: int = 6 * 10**8):
    if depends_on_path.suffix == ".gz":
        depends_on_ctx = gzip.open(depends_on_path, mode="rt", encoding="utf8")
    else:
        depends_on_ctx = depends_on_path.open(mode="r", encoding="utf8")
    with depends_on_ctx as depends_on:
        size = 0
        last_drv = 0  # Assumption: csv starts with drv_id=1
        last_pname = None
        indptr = np.zeros(n_drvs)
        columns = np.zeros(max_size)
        data = np.ones(max_size)
        ln: str
        for ln in depends_on:  # type: ignore
            drv_id, pname_id = [int(x) for x in ln.split(",")]

            if drv_id == last_drv + 1:
                indptr[drv_id] = size
            elif drv_id == last_drv:
                pass
            else:
                raise ValueError(
                    f"read_dag only supports contiguous csv's: {last_drv=} {drv_id=} {size=}"
                )

            columns[size] = pname_id
            # data[size] is already 1
            size += 1
            assert size < max_size

            last_drv, last_pname = drv_id, pname_id
        return sp.sparse.csr_array((data, columns, indptr), shape=(n_drvs, n_pnames))


if __name__ == "__main__":
    with ExitStack() as dtors:
        args = parser.parse_args()

        builds_path = (
            Path(args.input_dir, "builds_drv_and_time.csv.gz")
            if args.builds is None
            else Path(args.builds)
        )
        drvs_path = (
            Path(args.input_dir, "drvs.csv.gz")
            if args.drvs is None
            else Path(args.drvs)
        )
        depends_on_path = (
            Path(args.input_dir, "input_pnames.csv.gz")
            if args.depends_on is None
            else Path(args.depends_on)
        )
        pnames_path = (
            Path(args.input_dir, "pnames.csv.gz")
            if args.pnames is None
            else Path(args.pnames)
        )
        index_path = (
            Path(args.input_dir, "hydra.db")
            if args.index is None
            else Path(args.index)
        )

        if index_path.exists():
            index = dtors.enter_context(sqlite3.connect(index_path))
        else:
            index_path = None
            index = None

        # With uncompressed .csv inputs the following three (drv, pname, dependson) cost approximately...
        #
        # real    0m47,405s
        # user    0m42,278s
        # sys     0m4,653s
        if index_path is None:
            drvids = None
            drvpaths = None
            pname_ids = None
            pnames = None
        else:
            drvids = load_csv(drvs_path, dtype=np.dtype("int32"), usecols=(0,))
            drvpaths = load_csv(drvs_path, dtype=np.dtype("<U300"), usecols=(1,))
            pname_ids = load_csv(pnames_path, dtype=np.dtype("int32"), usecols=(0,))
            pnames = load_csv(pnames_path, dtype=np.dtype("<U300"), usecols=(1,))

        # depends_on = load_csv(depends_on_path)
        builds = load_csv(builds_path)
        n_builds = np.max(builds[:, 0])

        if index:
            (n_pnames,) = index.execute("SELECT MAX(pname_id) from PName").fetchone()
            (n_drvs,) = index.execute("SELECT MAX(drv_id) from Drv").fetchone()
        else:
            n_pnames: int = np.max(pname_ids)  # type: ignore
            n_drvs: int = np.max(drvids)  # type: ignore

        depends_on = read_dag(depends_on_path, n_drvs, n_pnames)

        n_deps = depends_on @ np.ones((n_pnames, 1))
        print(np.mean(n_deps))

        import matplotlib.pyplot as plt
        plt.plot(n_deps.ravel())
