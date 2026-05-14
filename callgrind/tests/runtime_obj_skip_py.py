"""Resolve libpython at runtime via sysconfig (mirrors pytest-codspeed's
approach in instruments/walltime.py), register it for obj-skip via the
client-request shim, then turn instrumentation on and run a small
integer workload.

We pass both the sysconfig path AND its os.path.realpath because
callgrind stores the realpath in obj_node->name (after symlink
resolution), and the runtime obj-skip check uses exact strcmp."""

import ctypes
import os
import sys
import sysconfig


def libpython_candidates() -> list[str]:
    ldlibrary = sysconfig.get_config_var("LDLIBRARY")
    libdir = sysconfig.get_config_var("LIBDIR")
    paths: list[str] = []
    if ldlibrary and libdir:
        paths.append(os.path.join(libdir, ldlibrary))
    if ldlibrary:
        paths.append(os.path.join(sys.prefix, "lib", ldlibrary))
    # Add realpath variants so the exact-match obj-skip finds the
    # file under whichever name the loader actually mapped.
    resolved: list[str] = []
    seen: set[str] = set()
    for p in paths:
        if not p:
            continue
        if p not in seen and os.path.exists(p):
            resolved.append(p)
            seen.add(p)
        try:
            r = os.path.realpath(p)
        except OSError:
            continue
        if r not in seen and os.path.exists(r):
            resolved.append(r)
            seen.add(r)
    return resolved


def main() -> None:
    here = os.path.dirname(os.path.abspath(__file__))
    shim = ctypes.CDLL(os.path.join(here, "runtime_obj_skip_py_shim.so"))
    shim.add_obj_skip.argtypes = [ctypes.c_char_p]
    shim.add_obj_skip.restype = None
    shim.start_instr.argtypes = []
    shim.start_instr.restype = None
    shim.stop_instr.argtypes = []
    shim.stop_instr.restype = None

    for path in libpython_candidates():
        shim.add_obj_skip(path.encode())
    if sys.executable:
        shim.add_obj_skip(sys.executable.encode())
        real = os.path.realpath(sys.executable)
        if real != sys.executable:
            shim.add_obj_skip(real.encode())

    shim.start_instr()
    acc = 0
    for i in range(10_000):
        acc = (acc + i * i) ^ (i << 1)
    shim.stop_instr()


if __name__ == "__main__":
    main()
