#!/bin/bash
# @file bootstrap.sh
#  @author Tim Hughes <tim@twistedfury.com>
#  @date 2014
# Script to fetch and compile depdencies for building Ethereum using Visual Studio Express 2013.
# Prerequisites:
#  - Visual Studio Express 2013 for Desktop
#  - On PATH: bash, git, git-svn, curl, sed, 7z

error_exit() {
    echo $1 1>&2
    exit 1
}

for i in python perl curl git sed 7z; do
	which $i &>/dev/null || error_exit "Could not find $i on PATH"
done

if [ ! -d "$VS120COMNTOOLS" ]; then
	error_exit "Couldn't find Visual Studio 2013"
fi

if [[ ! $@ ]] || [ $1 == "fetch" ]; then
	# fetch ethereum (develop branch)
	if [ ! -d cpp-ethereum ]; then
		(set -x; git clone https://github.com/ethereum/cpp-ethereum.git)
		cd cpp-ethereum
		(set -x; git checkout -b develop origin/develop)
		cd ..
		echo
	fi
	
	# fetch CryptoPP-5.6.2
	if [ ! -d cryptopp ]; then
		(set -x; git svn clone -r 541:541 http://svn.code.sf.net/p/cryptopp/code/trunk/c5 cryptopp)
		echo
	fi

	# fetch MiniUPnP-1.8
	if [ ! -d miniupnp ]; then
		(set -x; git clone https://github.com/miniupnp/miniupnp.git)
		cd miniupnp
		(set -x; git checkout tags/miniupnpd_1_8)
		cd ..
		echo
	fi

	# fetch LevelDB (windows branch)
	if [ ! -d leveldb ]; then
		(set -x; git clone https://code.google.com/p/leveldb/)
		cd leveldb
		(set -x; git checkout origin/windows)
		cd ..
		echo
	fi

	# fetch and unpack boost-1.55 source
	if [ ! -d boost ]; then
		if [ ! -f _download/boost_1_55_0.7z ]; then
			(set -x; mkdir -p _download)
			(set -x; curl -o _download/boost_1_55_0.7z -L http://sourceforge.net/projects/boost/files/boost/1.55.0/boost_1_55_0.7z/download)
		fi
		(set -x; 7z x _download/boost_1_55_0.7z)
		(set -x; mv boost_1_55_0 boost)
		echo
	fi

	# fetch and unpack Qt 5.1.2 source
	if [ ! -d Qt ]; then
		if [ ! -f _download/qt-everywhere-opensource-src-5.2.1.zip ]; then
			(set -x; mkdir -p _download)
			(set -x; curl -o _download/qt-everywhere-opensource-src-5.2.1.zip -L http://download.qt-project.org/official_releases/qt/5.2/5.2.1/single/qt-everywhere-opensource-src-5.2.1.zip)
		fi
		(set -x; mkdir Qt)
		cd Qt
		(set -x; 7z x ../_download/qt-everywhere-opensource-src-5.2.1.zip)
		(set -x; mv qt-everywhere-opensource-src-5.2.1 Src)
		# patch qmake.conf to use the static CRT
		(set -x; sed -i -e 's/-MD/-MT/g' Src/qtbase/mkspecs/win32-msvc2013/qmake.conf)
		cd ..
		echo
	fi
	
	# fetch jom
	if [ ! -f "Qt/jom/jom.exe" ]; then
		if [ ! -f "_download/jom.zip" ]; then
			(set -x; mkdir -p _download)
			(set -x; curl -o "_download/jom.zip" -L http://download.qt-project.org/official_releases/jom/jom.zip)
		fi
		(set -x; mkdir -p Qt/jom)
		cd Qt/jom
		(set -x; 7z x ../../_download/jom.zip)
		cd ../..
		echo
	fi
	
	# fetch and unpack Lua binaries
	if [ ! -d lua ]; then
		if [ ! -f _download/lua-5.2.1_Win32_bin.zip ]; then
			(set -x; mkdir -p _download)
			(set -x; curl -o _download/lua-5.2.1_Win32_bin.zip -L http://sourceforge.net/projects/luabinaries/files/5.2.1/Executables/lua-5.2.1_Win32_bin.zip/download)
		fi
		(set -x; mkdir lua)
		cd lua
		(set -x; 7z x ../_download/lua-5.2.1_Win32_bin.zip lua52.exe lua52.dll)
		(set -x; mv lua52.exe lua.exe)
		cd ..
		echo
	fi
fi

compile_boost()
{
	if [ $platform == "x64" ]; then
		addressModel="address-model=64"
	else
		addressModel=""
	fi
	
	if [ ! -d "stage/$platform" ]; then
		targets="--with-filesystem --with-system --with-thread --with-date_time --with-regex"
		(set -x; ./b2 -j4 --build-type=complete link=static runtime-link=static variant=debug,release threading=multi $addressModel $targets stage)
		(set -x; mv stage/lib stage/$platform)
	fi
}

if [[ ! $@ ]] || [ $1 == "compile-boost" ]; then
	# bootstrap if b2 is missing
	cd boost
	if [ ! -f "b2.exe" ]; then
		(set -x; cmd.exe /c bootstrap.bat)
	fi
	
	# compile boost for x86 and x64
	platform="x64"; compile_boost
	platform="Win32"; compile_boost
	cd ..
	echo
fi

compile_qt()
{
	if [ ! -d $platform ]; then
		(set -x; cmd.exe /c "..\\cpp-ethereum\\windows\\compile_qt.bat $platform")
	fi
}

if [[ ! $@ ]] || [ $1 == "compile-qt" ]; then
	# compile Qt for x86 and x64
	cd Qt
	platform="x64"; compile_qt
	platform="Win32"; compile_qt
	cd ..
	echo
fi

# finally run MS build
cd cpp-ethereum/windows
cmd.exe /c "compile_ethereum.bat"
cd ..