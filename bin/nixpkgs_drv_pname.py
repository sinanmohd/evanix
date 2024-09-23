#!/usr/bin/env python3

# This script is used to evaluate Nixpkgs and store the relationships between
# derivations and their input derivations in a directed acyclic graph (DAG).
# The DAG is represented using an adjacency list and saved in a SQLite db

import re
import sys
import subprocess
import json
import sqlite3
import argparse
from typing import Set

class drv:
    drv: str
    pname: str
    input_drv_pnames: Set[str]
    cursor: sqlite3.Cursor

    def __init__(self, drv_string: str, cursor: sqlite3.Cursor) -> None:
        self.input_drv_pnames = set()
        self.cursor = cursor

        j = json.loads(drv_string)
        if 'error' in j:
            raise TypeError(f'{j['attrPath']}: Failed to evaluate')
        elif 'drvPath' not in j:
            raise TypeError(f'{j['attrPath']}: Failed to read drvPath')

        self.drv = str(j['drvPath'])
        pname = self.pname_from_drv_path(j['drvPath'])
        if pname is None:
            raise TypeError(f'{j['attrPath']}: Failed to read pname')
        print(pname)
        self.pname = pname

        for input_drv in j['inputDrvs']:
            pname = self.pname_from_drv_path(input_drv)
            if pname is not None:
                self.input_drv_pnames.add(pname)

    def db_push(self):
        pname_id = self.rowid_from_pname(self.cursor, self.pname)
        ret =self.cursor.execute("""
            INSERT INTO drvs (drv, pname_id)
            VALUES (?, ?)
        """, (self.drv, pname_id))
        drv_id = ret.lastrowid
        if drv_id is None:
            raise ValueError

        for pname in self.input_drv_pnames:
            pname_id = self.rowid_from_pname(self.cursor, pname)
            ret = self.cursor.execute("""
                INSERT INTO input_pnames (drv_id, pname_id)
                VALUES (?, ?)
            """,  (drv_id, pname_id))

    @staticmethod
    def rowid_from_pname(cursor: sqlite3.Cursor, pname: str) -> int:
        s = cursor.execute("""
            SELECT pnames.ROWID FROM pnames
            WHERE pnames.pname = ?
        """, (pname,))
        id = s.fetchone()
        if id:
            return id[0]
        
        s = cursor.execute("""
            INSERT INTO pnames (pname)
            VALUES (?)
        """, (pname,))
        if s.lastrowid is None:
            raise TypeError('Failed to get lastrowid')

        return s.lastrowid

    @staticmethod
    def pname_from_drv_path(drv_path: str) -> str | None:
        f = open(drv_path, 'r')
        drv_string = f.readline()
        f.close()
        match = re.search('"pname","([^"]+)', drv_string)
        if match is not None:
            return match.group(1)

def args_get():
    parser = argparse.ArgumentParser()
    parser.add_argument('-r', '--ref', default="master")
    parser.add_argument('-a', '--arch', default="x86_64-linux")
    parser.add_argument('-d', '--db', default="drv_pname_dag.db")
    return parser.parse_args()

if __name__ == '__main__':
    args = args_get()
    cmd = [
        'nix-eval-jobs',
        '--flake',
        f'github:nixos/nixpkgs/{args.ref}#legacyPackages.{args.arch}'
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL)
    if proc.stdout is None:
        raise EOFError('Failed to evaluate nixpkgs')

    con = sqlite3.connect(args.db)
    cur = con.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS pnames (
            pname TEXT NOT NULL UNIQUE,
            UNIQUE(pname) ON CONFLICT REPLACE
        )
    """)
    cur.execute("""
        CREATE TABLE IF NOT EXISTS drvs (
            drv TEXT NOT NULL,
            pname_id INTEGER NOT NULL,
            FOREIGN KEY(pname_id) REFERENCES pnames(ROWID),
            UNIQUE(drv) ON CONFLICT REPLACE
        )
    """)
    cur.execute("""
        CREATE TABLE IF NOT EXISTS input_pnames (
            drv_id INTEGER NOT NULL,
            pname_id INTEGER NOT NULL,
            FOREIGN KEY(drv_id) REFERENCES drvs(ROWID),
            FOREIGN KEY(pname_id) REFERENCES pnames(ROWID),
            UNIQUE(drv_id, pname_id) ON CONFLICT REPLACE
        )
    """)

    for line in proc.stdout:
        try:
            d = drv(line.decode('utf-8'), cur)
            d.db_push()
        except Exception as e:
            print(f'>>> {e}', file=sys.stderr)

    con.commit()
    con.close()
