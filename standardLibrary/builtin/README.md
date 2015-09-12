The Cardinal scripts in this directory get converted to C string literals and then
inserted into their respective .c files so that the interpreter can load them
directly without having to do any file IO.

The script that does this copying is `build/generate_builtins.py`.