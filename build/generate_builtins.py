#!/usr/bin/env python

import glob
import os.path
import re
import sys

PATTERN = re.compile(r'libSource =\n("(.|[\n])*?);')

root = sys.argv[1]

def copy_builtin(filename):
  name = os.path.basename(filename)
  name = name.split('.')[0]

  with open(filename, "r") as f:
    lines = f.readlines()

  udog_source = ""
  for line in lines:
    line = line.replace('"', "\\\"")
    line = line.replace("\r\n", "\\n\"")
    if udog_source: udog_source += "\n"
    udog_source += '"' + line

  # re.sub() will unescape escape sequences, but we want them to stay escapes
  # in the C string literal.
  udog_source = udog_source.replace('\\', '\\\\')

  constant = "libSource =\n" + udog_source + ";"

  with open(root + "src/vm/cardinal_" + name + ".c", "r") as f:
    c_source = f.read()

  c_source = PATTERN.sub(constant, c_source)

  with open(root + "src/vm/cardinal_" + name + ".txt", "w") as f:
    f.write(constant)

  print(name)


for f in glob.iglob(root + "standardLibrary/builtin/*.tus"):
  copy_builtin(f)
