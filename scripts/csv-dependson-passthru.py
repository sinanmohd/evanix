#!/usr/bin/env python3

"""
❯ time python bin/csv-dependson-passthru.py 
595864145it [02:47, 3551210.13it/s]

real    2m47,925s
user    2m43,734s
sys     0m3,116s
"""

from argparse import ArgumentParser
import tqdm

parser = ArgumentParser("csv-dependson-passthru")
parser.add_argument("--input_pnames", default="data/input_pnames.csv")

if __name__ == "__main__":
    args = parser.parse_args()
    with open(args.input_pnames, "r", encoding="utf8") as f:
        for line in tqdm.tqdm(f):
            pass
