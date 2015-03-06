#! /bin/bash

LMOD=$(realpath $(dirname $0)/..)
find $LMOD -path "$LMOD/.git*" -prune -o -type f -print
