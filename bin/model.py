#!/usr/bin/env python3

from typing import List, Tuple
from sklearn import linear_model
import tqdm
import scipy
import sqlite3
import argparse
import numpy as np

class matrix_builds_inputpnames:
    independent_variables: scipy.sparse.coo_array
    dependent_variable: np.ndarray

    def __init__(self, db: str) -> None:
        row: List[int] = []
        col: List[int] = []
        data: List[int] = []

        con = sqlite3.connect(db)
        cur = con.cursor()

        builds = self.table_fetch(cur, '''
            SELECT ROWID, drv_id, duration FROM builds_cleaned
        ''')
        self.dependent_variable = np.array([ [ row[1] ] for row in tqdm.tqdm(builds) ])

        for build in tqdm.tqdm(builds):
            inputpnames = self.table_fetch(cur, '''
                SELECT drv_id, pname_id FROM input_pnames
                WHERE input_pnames.drv_id = ?
            ''', (build[1],) )

            for inputpname in inputpnames:
                row.append(build[0] - 1)
                col.append(inputpname[1])
                data.append(1)

        np_row = np.array(row)
        np_col = np.array(col)
        np_data = np.array(data)
        self.independent_variables = scipy.sparse.coo_array((np_data, (np_row, np_col)))

        con.commit()
        con.close()

    @staticmethod
    def table_fetch(cur: sqlite3.Cursor, query: str, args: Tuple = ()) -> List:
        query_exec = cur.execute(query, args)
        return query_exec.fetchall()

def args_get():
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', '--db', required=True, help="path to sqlite database containing input_pnames and builds")
    parser.add_argument('-t', '--test', type=int, required=True, help="row to test against")
    return parser.parse_args()

if __name__ == '__main__':
    args = args_get()
    matrix = matrix_builds_inputpnames(args.db)

    regr = linear_model.LinearRegression()
    regr.fit(matrix.independent_variables, matrix.dependent_variable)
    pred = regr.predict([matrix.independent_variables.toarray()[args.test]])

    print(f'prediction  : {pred[0][0]}')
    print(f'og duration : {matrix.dependent_variable[args.test][0]}')

