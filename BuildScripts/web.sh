#!/bin/bash -ex

[ "$FO_SOURCE" ] || { echo "FO_SOURCE is empty"; exit 1; }
[ "$FO_BUILD_DEST" ] || { echo "FO_BUILD_DEST is empty"; exit 1; }

export SOURCE_FULL_PATH=$(cd $FO_SOURCE; pwd)

sudo apt-get -y update || true
sudo apt-get -y install build-essential
sudo apt-get -y install cmake
sudo apt-get -y install wput
sudo apt-get -y install python2.7
sudo apt-get -y install nodejs
sudo apt-get -y install default-jre
sudo apt-get -y install git-core

mkdir -p $FO_BUILD_DEST
cd $FO_BUILD_DEST
mkdir -p web
cd web
mkdir -p Web
rm -rf Web/*
cp -r "$SOURCE_FULL_PATH/BuildScripts/web/." "./Web/"

mkdir -p emsdk
cp -r "$SOURCE_FULL_PATH/BuildScripts/emsdk" "./"
cd emsdk
chmod +x ./emsdk
./emsdk update
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
cd ../
emcc -v

mkdir -p release
cd release
cmake -G "Unix Makefiles" -C "$SOURCE_FULL_PATH/BuildScripts/web.cache.cmake" "$SOURCE_FULL_PATH/Source"
make -j4
cd ../

mkdir -p debug
cd debug
cmake -G "Unix Makefiles" -C "$SOURCE_FULL_PATH/BuildScripts/web.cache.cmake" -DFO_DEBUG=ON "$SOURCE_FULL_PATH/Source"
make -j4
cd ../

if [ -n "$FO_FTP_DEST" ]; then
	wput Web ftp://$FO_FTP_USER@$FO_FTP_DEST/Client/
fi

if [ -n "$FO_COPY_DEST" ]; then
	cp -r Web "$FO_COPY_DEST/Client"
fi
