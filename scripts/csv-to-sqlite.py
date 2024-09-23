#!/usr/bin/env python3

"""
❯ time python bin/csv-to-sqlite.py
PName: 43208it [00:00, 271163.46it/s]
Drv: 55964it [00:00, 226297.07it/s]
Build: 178366it [00:00, 220148.71it/s]
DependsOn: 595864145it [35:46, 277540.28it/s]

real    35m48,590s
user    35m14,202s
sys     0m19,561s
"""

from argparse import ArgumentParser
import sqlite3
from pathlib import Path
import tqdm


parser = ArgumentParser("csv-to-sqlite")
parser.add_argument("--input-dir", default=Path("data/"))
parser.add_argument("--builds", default=None, help="data/builds_drv_and_time.csv")
parser.add_argument("--drvs", default=None, help="data/drvs.csv")
parser.add_argument("--input_pnames", default=None, help="data/input_pnames.csv")
parser.add_argument("--pnames", default=None, help="data/pnames.csv")
parser.add_argument("--db", default=Path("data/hydra.db"))

SCHEMA = """
DROP TABLE IF EXISTS DependsOn;
CREATE TABLE IF NOT EXISTS DependsOn(drv_id INTEGER, pname_id INTEGER, PRIMARY KEY(drv_id, pname_id));

DROP TABLE IF EXISTS Drv;
CREATE TABLE IF NOT EXISTS Drv(drv_id INTEGER PRIMARY KEY, drvpath TEXT UNIQUE);

DROP TABLE IF EXISTS PName;
CREATE TABLE IF NOT EXISTS PName(pname_id INTEGER PRIMARY KEY, pname TEXT UNIQUE);

DROP TABLE IF EXISTS Build;
CREATE TABLE IF NOT EXISTS Build(build_id INTEGER PRIMARY KEY, starttime INTEGER, stoptime INTEGER, drv_id INTEGER)
"""


def read_int_str(path):
    with open(path, "r", encoding="utf8") as f:
        for line in f:
            i, x = line.split(",")
            yield int(i), x

def read_ni32(path):
    with open(path, "r", encoding="utf8") as f:
        for line in f:
            tokens = line.split(",")
            yield tuple(int(x) for x in tokens)


if __name__ == "__main__":
    args = parser.parse_args()

    builds_path = (
        Path(args.input_dir, "builds_drv_and_time.csv")
        if args.builds is None
        else Path(args.builds)
    )
    drvs_path = (
        Path(args.input_dir, "drvs.csv") if args.drvs is None else Path(args.drvs)
    )
    input_pnames_path = (
        Path(args.input_dir, "input_pnames.csv")
        if args.input_pnames is None
        else Path(args.input_pnames)
    )
    pnames_path = (
        Path(args.input_dir, "pnames.csv") if args.pnames is None else Path(args.pnames)
    )

    with sqlite3.connect(args.db) as con:
        con.executescript(SCHEMA)
        con.commit()


        con.executemany("INSERT INTO PName(pname_id, pname) VALUES (?, ?)", tqdm.tqdm(read_int_str(pnames_path), desc="PName"))
        con.executemany("INSERT INTO Drv(drv_id, drvpath) VALUES (?, ?)", tqdm.tqdm(read_int_str(drvs_path), desc="Drv"))
        con.executemany("INSERT INTO Build(build_id, starttime, stoptime, drv_id) VALUES (?, ?, ?, ?)", tqdm.tqdm(read_ni32(builds_path), desc="Build"))
        con.executemany("INSERT INTO DependsOn(drv_id, pname_id) VALUES (?, ?)", tqdm.tqdm(read_ni32(input_pnames_path), desc="DependsOn"))
