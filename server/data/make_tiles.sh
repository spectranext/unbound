#!/bin/sh
set -e

mkdir -p ../pages/

if test -d ~/Dropbox/unbound; then
  cp -R ~/Dropbox/unbound/* .
fi

CLIENT_MODULES_BIN="${CLIENT_MODULES_BIN:-../../client/cmake-build-release}"
# CLIENT_BIN="${CLIENT_BIN:-../../client/cmake-build-debug}"

# rebuild the modules
make -C ${CLIENT_MODULES_BIN} all_modules

# for docs
mkdir -p ../python/server/doc
python3 ../../common/tools/databuild.py ./docs.yml ../python/server/doc/icons
python3 ../../common/tools/databuild.py ./data.yml ../pages/data

CLIENT_BIN=${CLIENT_MODULES_BIN} python3 ../../common/tools/databuild.py ./modules.yml ../pages/modules
APPEND=1 python3 ../../common/tools/databuild.py ./music.yml ../pages/modules

python3 ../../common/tools/genicons.py icons ../../common/tools/png2c/png2c.py ../python/server/icons.py
python3 ../../common/tools/genart.py art ../../common/tools/png2c/png2c.py ../python/server/art.py
