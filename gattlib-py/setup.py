#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2016-2024, Olivier Martin <olivier@labapart.org>
#

import os
import re
import subprocess
import sys
from pathlib import Path

from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext
import subprocess

SETUP_DIR = os.path.dirname(os.path.realpath(__file__))

# Name of the directory containing the python sources
python_module_name = "gattlib"
# Specified where the CMakeLists.txt is located
native_source_dir = os.environ.get("NATIVE_SOURCE_DIR", ".")

# Retrieve version from GIT
git_version_command = subprocess.Popen(['git', 'describe', '--abbrev=7', '--dirty', '--always', '--tags'],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
stdout, stderr = git_version_command.communicate()
if git_version_command.returncode == 0:
    git_version = stdout.decode('utf-8').strip()
else:
    git_version = None

#
# Create '_version.py'
#
package_version = os.environ.get('GATTLIB_PY_VERSION', git_version)

GATTLIB_VERSION_FILE = os.path.join(SETUP_DIR, "gattlib", "_version.py")

# Case we are building from source package
if package_version is None:
    with open(GATTLIB_VERSION_FILE, "r") as f:
        gattlib_version_statement = f.read()
        res = re.search(r'__version__ = "(.*)"', gattlib_version_statement)
        package_version = res.group(1)

if package_version:
    with open(GATTLIB_VERSION_FILE, "w") as f:
        f.write(f"__version__ = \"{package_version}\"\n")


class CMakeExtension(Extension):
    """Custom extension class that allows to specify the root folder of the CMake project."""
    def __init__(self, name, sourcedir='.', **kwa):
        Extension.__init__(self, name, sources=[], **kwa)
        self.sourcedir = os.path.abspath(sourcedir)

# Convert distutils Windows platform specifiers to CMake -A arguments
PLAT_TO_CMAKE = {
    "win32": "Win32",
    "win-amd64": "x64",
    "win-arm32": "ARM",
    "win-arm64": "ARM64",
}

class CMakeBuild(build_ext):
    def build_extension(self, ext: CMakeExtension) -> None:
        try:
            _ = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError('Cannot find CMake executable')

        # Must be in this form due to bug in .resolve() only fixed in Python 3.10+
        ext_fullpath = Path.cwd() / self.get_ext_fullpath(ext.name)
        extdir = ext_fullpath.parent.resolve()

        # Using this requires trailing slash for auto-detection & inclusion of
        # auxiliary "native" libs

        debug = int(os.environ.get("DEBUG", 0)) if self.debug is None else self.debug
        cfg = "Debug" if debug else "Release"

        # CMake lets you override the generator - we need to check this.
        # Can be set with Conda-Build, for example.
        cmake_generator = os.environ.get("CMAKE_GENERATOR", "")

        cmake_library_output_dir = Path(str(extdir), ext.name)

        # Set Python_EXECUTABLE instead if you use PYBIND11_FINDPYTHON
        # EXAMPLE_VERSION_INFO shows you how to pass a value into the C++ code
        # from Python.
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={cmake_library_output_dir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE={cfg}",  # not used on MSVC, but no harm
            "-DGATTLIB_PYTHON_INTERFACE=ON",
            "-DGATTLIB_BUILD_EXAMPLES=OFF",
        ]
        build_args = []
        # Adding CMake arguments set as environment variable
        # (needed e.g. to build for ARM OSx on conda-forge)
        if "CMAKE_ARGS" in os.environ:
            cmake_args += [item for item in os.environ["CMAKE_ARGS"].split(" ") if item]

        # In this example, we pass in the version to C++. You might not need to.
        cmake_args += [
            f"-DEXAMPLE_VERSION_INFO={self.distribution.get_version()}",
            "-DGATTLIB_BUILD_EXAMPLES=NO",
            "-DGATTLIB_LOG_BACKEND=python"]

        if self.compiler.compiler_type != "msvc":
            # Using Ninja-build since it a) is available as a wheel and b)
            # multithreads automatically. MSVC would require all variables be
            # exported for Ninja to pick it up, which is a little tricky to do.
            # Users can override the generator with CMAKE_GENERATOR in CMake
            # 3.15+.
            if not cmake_generator or cmake_generator == "Ninja":
                try:
                    import ninja

                    ninja_executable_path = Path(ninja.BIN_DIR) / "ninja"
                    cmake_args += [
                        "-GNinja",
                        f"-DCMAKE_MAKE_PROGRAM:FILEPATH={ninja_executable_path}",
                    ]
                except ImportError:
                    pass

        else:
            # Single config generators are handled "normally"
            single_config = any(x in cmake_generator for x in {"NMake", "Ninja"})

            # CMake allows an arch-in-generator style for backward compatibility
            contains_arch = any(x in cmake_generator for x in {"ARM", "Win64"})

            # Specify the arch if using MSVC generator, but only if it doesn't
            # contain a backward-compatibility arch spec already in the
            # generator name.
            if not single_config and not contains_arch:
                cmake_args += ["-A", PLAT_TO_CMAKE[self.plat_name]]

            # Multi-config generators have a different way to specify configs
            if not single_config:
                cmake_args += [
                    f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{cfg.upper()}={extdir}"
                ]
                build_args += ["--config", cfg]

        if sys.platform.startswith("darwin"):
            # Cross-compile support for macOS - respect ARCHFLAGS if set
            archs = re.findall(r"-arch (\S+)", os.environ.get("ARCHFLAGS", ""))
            if archs:
                cmake_args += ["-DCMAKE_OSX_ARCHITECTURES={}".format(";".join(archs))]

        # Set CMAKE_BUILD_PARALLEL_LEVEL to control the parallel build level
        # across all generators.
        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            # self.parallel is a Python 3 only way to set parallel jobs by hand
            # using -j in the build_ext call, not supported by pip or PyPA-build.
            if hasattr(self, "parallel") and self.parallel:
                # CMake 3.12+ only.
                build_args += [f"-j{self.parallel}"]

        build_temp = Path(self.build_temp) / ext.name
        if not build_temp.exists():
            build_temp.mkdir(parents=True)

        subprocess.run(
            ["cmake", ext.sourcedir, *cmake_args], cwd=build_temp, check=True
        )
        subprocess.run(
            ["cmake", "--build", ".", "--target", "gattlib", *build_args], cwd=build_temp, check=True
        )

setup(
    name='gattlib-py',
    version=package_version,
    author="Olivier Martin",
    author_email="olivier@labapart.com",
    description="Python wrapper for gattlib library",
    url="https://github.com/labapart/gattlib/gattlib-py",
    long_description=open(os.path.join(SETUP_DIR, 'README.md')).read(),
    long_description_content_type='text/markdown',
    packages=find_packages(),
    install_requires=[
        'setuptools',
        'PyGObject>=3.44.0'
    ],
    ext_modules=[CMakeExtension(python_module_name, sourcedir=native_source_dir)],
    cmdclass={'build_ext': CMakeBuild},
)
