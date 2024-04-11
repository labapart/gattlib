#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Retrieve path of current script
SCRIPT_PATH=`dirname "$0"`
SCRIPT_PATH=`( cd "$SCRIPT_PATH" && pwd )`
ROOT_PATH=`( cd "$SCRIPT_PATH/.." && pwd )`

gattlib_py_package_dir=$(mktemp -d -p $PWD -t gattlib-py-package-XXXXXXXXXX)

# Python code
cp -r ${ROOT_PATH}/gattlib-py/gattlib ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/gattlib-py/setup.py ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/gattlib-py/README.md ${gattlib_py_package_dir}/

# Native code
cp -r ${ROOT_PATH}/common ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/bluez ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/dbus ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/include ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/CMakeLists.txt ${gattlib_py_package_dir}/
cp -r ${ROOT_PATH}/CrossCompilation.cmake ${gattlib_py_package_dir}/

# Build script
mkdir ${gattlib_py_package_dir}/ci/
cp -r ${ROOT_PATH}/ci/install-bluez.sh ${gattlib_py_package_dir}/ci/

# Create MANIFEST.in
rm -f ${gattlib_py_package_dir}/MANIFEST.in
cat <<EOT >> ${gattlib_py_package_dir}/MANIFEST.in
graft common
graft bluez
graft dbus
graft include
include CMakeLists.txt
include CrossCompilation.cmake
EOT

# Install requirements
python3 -m pip --disable-pip-version-check install cibuildwheel==2.16.5

# Generate packages
pushd ${gattlib_py_package_dir}

# Binary package
python3 -m cibuildwheel --output-dir dist

# Source package
python setup.py sdist

# Move generated artifact to project root path
rm -Rf ${ROOT_PATH}/dist
mv dist ${ROOT_PATH}

popd

rm -Rf ${gattlib_py_package_dir}
