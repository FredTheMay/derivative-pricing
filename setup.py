"""Builds the `mcd` pybind11 extension module directly from the engine's own C++ sources.

Deliberately bypasses the project's CMake build (which exists for the C++ test/benchmark
suite and its GoogleTest/Google Benchmark FetchContent dependencies -- neither of which the
Python extension needs). pybind11's own setup_helpers, bundled inside the `pybind11`
package CLAUDE.md already approves (docs/design/06-cli-bindings-reporting.md sec.4), is the
only thing this needs beyond setuptools itself.
"""

from pybind11.setup_helpers import Pybind11Extension, build_ext
from setuptools import setup

ENGINE_SOURCES = [
    "src/core/types.cpp",
    "src/core/normal.cpp",
    "src/core/rng.cpp",
    "src/core/thread_pool.cpp",
    "src/core/linalg.cpp",
    "src/models/gbm.cpp",
    "src/pricers/analytic.cpp",
    "src/pricers/binomial.cpp",
    "src/pricers/monte_carlo.cpp",
    "src/pricers/lsm.cpp",
    "src/greeks/finite_difference.cpp",
]

ext_modules = [
    Pybind11Extension(
        "mcd",
        ENGINE_SOURCES + ["bindings/python/src/module.cpp"],
        include_dirs=["include"],
        define_macros=[("MCD_VERSION_STRING", '"0.1.0"')],
        cxx_std=20,
        libraries=["pthread"] if not __import__("sys").platform.startswith("darwin") else [],
    ),
]

setup(ext_modules=ext_modules, cmdclass={"build_ext": build_ext})
