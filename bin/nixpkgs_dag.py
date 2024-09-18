#!/usr/bin/env python3

import re
import subprocess
import json
import sqlite3
from typing import Set

class drv:
    pname: str
    input_drvs: Set[str] = set()
    cursor: sqlite3.Cursor

    def __init__(self, drv_string: str, cursor: sqlite3.Cursor) -> None:
        self.cursor = cursor

        j = json.loads(drv_string)
        if 'drvPath' not in j:
            raise TypeError

        pname = self.pname_from_drv_path(j['drvPath'])
        print(pname)
        if pname is None:
            raise TypeError
        self.pname = pname

        for input_drv in j['inputDrvs']:
            pname = self.pname_from_drv_path(input_drv)
            if pname is not None:
                self.input_drvs.add(pname)

    def db_push(self):
        parrent_id = self.rowid_from_pname(self.cursor, self.pname)
        for input_drv in self.input_drvs:
            child_id = self.rowid_from_pname(self.cursor, input_drv)

            self.cursor.execute("""
                INSERT INTO edges (start, end)
                VALUES (?, ?)
            """, (parrent_id, child_id))

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
            raise TypeError

        return s.lastrowid

    @staticmethod
    def pname_from_drv_path(drv_path: str) -> str | None:
        f = open(drv_path, 'r')
        drv_string = f.readline()
        match = re.search('"pname","([^"]+)', drv_string)
        if match is not None:
            return match.group(1)

if __name__ == '__main__':
    cmd = ['nix-eval-jobs', '--flake', 'github:nixos/nixpkgs#legacyPackages.x86_64-linux']
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if proc.stdout is None:
        raise TypeError

    con = sqlite3.connect('nixpkgs_dag.db')
    cur = con.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS pnames (
            pname TEXT NOT NULL UNIQUE
        )
    """)
    cur.execute("""
        CREATE TABLE IF NOT EXISTS edges (
            start INTEGER NOT NULL,
            end NOT NULL,
            UNIQUE(start, end) ON CONFLICT REPLACE
        )
    """)

    for line in proc.stdout:
        try:
            d = drv(line.decode('utf-8'), cur)
            d.db_push()
        except Exception as e:
            print(str(e))

    con.commit()
    con.close()
