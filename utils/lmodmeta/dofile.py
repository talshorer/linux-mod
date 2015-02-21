#! /usr/bin/python

import sys

path = sys.argv[1]
lines = open(path, "rb").read().splitlines()

shevincludes = False
for i, line in enumerate(lines):
	if line.startswith("#include <"):
		shevincludes = True
	elif shevincludes:
		break
lines.insert(i, "#include <lmod/meta.h>")
lines.insert(i, "")

remove_lines = ("MODULE_AUTHOR", "MODULE_LICENSE")
lines = [line for line in lines if not line.startswith(remove_lines)]

lines.insert(-2, "LMOD_MODULE_META();")
lines.append("")


open(path, "wb").write("\n".join(lines))
