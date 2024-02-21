#!/bin/bash

#
# To launch: docker run --volume $PWD:/code --env GATTLIB_PY_VERSION=0.4 -it quay.io/pypa/manylinux_2_28_x86_64 /code/ci/generate-python-mainlinux-package.sh
#
# Based on https://github.com/pypa/python-manylinux-demo/blob/master/travis/build-wheels.sh
#

# Exit immediately if a command exits with a non-zero status.
set -ex

export PLAT=manylinux_2_28_x86_64

# Install dependencies
yum -y install wget dbus-devel

#
#
#
wget http://www.kernel.org/pub/linux/bluetooth/bluez-5.66.tar.xz
tar -xf bluez-5.66.tar.xz
pushd bluez-5.66
./configure --prefix=/usr --disable-obex --disable-udev --disable-cups --disable-client --disable-manpages --disable-tools --disable-obex --disable-monitor --disable-hog --disable-hid --disable-network --disable-a2dp --disable-avrcp --disable-bap --disable-mcp --disable-vcp --enable-library
make
make install
popd
rm -Rf bluez-5.66

#
# Generate Python package
#

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

# Create MANIFEST.in
cat <<EOT >> MANIFEST.in
graft common
graft bluez
graft dbus
graft include
include CMakeLists.txt
include CrossCompilation.cmake
EOT

mkdir -p wheelhouse
mkdir -p ${ROOT_PATH}/dist

function repair_wheel {
    wheel="$1"
    if ! auditwheel show "$wheel"; then
        echo "Skipping non-platform wheel $wheel"
    else
        auditwheel repair "$wheel" --plat "$PLAT" -w ${ROOT_PATH}/dist/
    fi
}

pushd ${gattlib_py_package_dir}

# For now, remove Pypy implementation. Only keep CPython implementation
# There is a build issue when using Pypy - CMake does not find the Python developer package.
rm -Rf /opt/python/pp*

# Compile wheels
for PYBIN in /opt/python/*/bin; do
    #"${PYBIN}/pip" install -r /io/dev-requirements.txt
    PATH=${PYBIN}:$PATH pip wheel . --no-deps -w wheelhouse/
done

# Bundle external shared libraries into the wheels
for whl in wheelhouse/*.whl; do
    repair_wheel "$whl"
done

# Install packages and test
for PYBIN in /opt/python/*/bin/; do
    "${PYBIN}/pip" install gattlib_py --no-index -f ${ROOT_PATH}/dist
    #(cd "$HOME"; "${PYBIN}/nosetests" pymanylinuxdemo)
done

popd

rm -Rf ${gattlib_py_package_dir}

ls ${ROOT_PATH}/dist
