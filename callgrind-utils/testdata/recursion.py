# Python counterpart to recursion.c: the same fib/square/compute shape, driven
# the way CodSpeed drives a benchmark. Instrumentation is off at startup (run
# with --instr-atstart=no) and turned on around the measured region via the
# clgctl shim, whose compiled path is passed as argv[1].
#
# Before starting, we skip the Python runtime objects (libpython + the python
# executable) from Callgrind at runtime, exactly as pytest-codspeed's
# instrument-hooks does in _callgrind_skip_python_runtime: the interpreter's own
# C frames are folded into their callers so they don't obfuscate the graph.
# Matching is by exact realpath, since Callgrind keys obj-skip on the mapped
# object path.
import ctypes
import os
import sys
import sysconfig

clgctl = ctypes.CDLL(sys.argv[1])


def skip_python_runtime():
    ldlibrary = sysconfig.get_config_var("LDLIBRARY")
    libdir = sysconfig.get_config_var("LIBDIR")
    libpython = next(
        (
            p
            for p in (
                os.path.join(libdir, ldlibrary) if ldlibrary and libdir else None,
                os.path.join(sys.prefix, "lib", ldlibrary) if ldlibrary else None,
            )
            if p and os.path.exists(p)
        ),
        None,
    )
    for path in (libpython, sys.executable):
        if path:
            clgctl.clg_add_obj_skip(os.path.realpath(path).encode())


def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)


def square(n):
    return n * n


def compute(n):
    return fib(n) + square(n)


skip_python_runtime()

clgctl.clg_start()
sink = compute(20)
clgctl.clg_stop()

assert sink == 7165, sink
