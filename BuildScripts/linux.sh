#!/bin/bash -ex

[ "$FO_SOURCE" ] || { echo "FO_SOURCE is empty"; exit 1; }
[ "$FO_BUILD_DEST" ] || { echo "FO_BUILD_DEST is empty"; exit 1; }

export SOURCE_FULL_PATH=$(cd $FO_SOURCE; pwd)

sudo apt-get -y update || true
sudo apt-get -y install build-essential
sudo apt-get -y install cmake
sudo apt-get -y install wput
sudo apt-get -y install libx11-dev
sudo apt-get -y install freeglut3-dev
sudo apt-get -y install libssl-dev
sudo apt-get -y install libevent-dev
sudo apt-get -y install libxi-dev

mkdir -p $FO_BUILD_DEST
cd $FO_BUILD_DEST
mkdir -p linux
cd linux
rm -rf Client/*
rm -rf Server/*
rm -rf Mapper/*
rm -rf ASCompiler/*

mkdir -p x64
cd x64
cmake -G "Unix Makefiles" -C "$SOURCE_FULL_PATH/BuildScripts/linux64.cache.cmake" "$SOURCE_FULL_PATH/Source"
make -j4
cd ../

# x86
sudo apt-get -y install gcc-multilib
sudo apt-get -y install g++-multilib
sudo apt-get -y install libx11-dev:i386
sudo apt-get -y install freeglut3-dev:i386
sudo apt-get -y install libssl-dev:i386
sudo apt-get -y install libevent-dev:i386
sudo apt-get -y install libxi-dev:i386

mkdir -p x86
cd x86
cmake -G "Unix Makefiles" -C "$SOURCE_FULL_PATH/BuildScripts/linux32.cache.cmake" "$SOURCE_FULL_PATH/Source"
make -j4
cd ../

if [ -n "$FO_FTP_DEST" ]; then
	wput Client --basename=Client/ ftp://$FO_FTP_USER@$FO_FTP_DEST/Client/Linux/
	wput Server ftp://$FO_FTP_USER@$FO_FTP_DEST/
	wput Mapper ftp://$FO_FTP_USER@$FO_FTP_DEST/
	wput ASCompiler ftp://$FO_FTP_USER@$FO_FTP_DEST/
fi

if [ -n "$FO_COPY_DEST" ]; then
	cp -r ./Client/. "$FO_COPY_DEST/Client/Linux"
	cp -r ./Server/. "$FO_COPY_DEST/Server"
	cp -r ./Mapper/. "$FO_COPY_DEST/Mapper"
	cp -r ./ASCompiler/. "$FO_COPY_DEST/ASCompiler"
fi
