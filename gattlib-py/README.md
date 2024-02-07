`gattlib-py` is a wrapper for `gattlib` library.

Development
-----------

1. Set `PYTHONPATH`: `export PYTHONPATH=$PWD/gattlib-py:$PYTHONPATH`

2. Build native library

```
mkdir -p build && cd build
cmake ..
make
```

3. Set `export LD_LIBRARY_PATH=$PWD/dbus` for Python module to find native library.