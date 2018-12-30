#!/bin/bash

createChangelogTemplate()
{
	BBP_VERSION=${1}
	BBP_NETWORK=${2}
	DISTRO=${3}
	DEB_VER=${4}
	DATE=$(date +"%a, %d %b %Y %H:%M:%S")
	
	echo "${BBP_NETWORK} (${BBP_VERSION}-${DISTRO}${DEB_VER}) ${DISTRO}; urgency=low

  * 
  * 

 -- Mobile Support <mobile@biblepay.org>  ${DATE}" > ./debian/changelog
}

#sudo apt install devscripts build-essential debhelper
BBP_VERSION=${1}
BBP_NETWORK=${2-mainnet}
BBP_BRANCH=${3-master}
DISTRO=${4-xenial}
DEBIAN_VERSION=${5-1}

if [ -z "$BBP_VERSION" ]; then
	echo "ERROR: Version is required"; 
	exit;
fi

if [ ! -d ${BBP_NETWORK}_${BBP_VERSION} ]; then
	rm ${BBP_BRANCH}.tar.gz
	wget https://github.com/biblepay/biblepay/archive/${BBP_BRANCH}.tar.gz
	tar -xzf ${BBP_BRANCH}.tar.gz
	mv biblepay-${BBP_BRANCH} ${BBP_NETWORK}_${BBP_VERSION}
	if [ ! -L ${BBP_NETWORK}_${BBP_VERSION}.orig.tar.gz ]; then  
		ln -s ${BBP_BRANCH}.tar.gz ${BBP_NETWORK}_${BBP_VERSION}.orig.tar.gz
	fi
fi

cd ${BBP_NETWORK}_${BBP_VERSION}

if [ ! -d "debian" ]; then
	cp -r ./contrib/debian ./debian
fi

createChangelogTemplate $BBP_VERSION $BBP_NETWORK $DISTRO $DEBIAN_VERSION
vi ./debian/changelog
debuild -S
cd ..
dput ppa:biblepay-official/${BBP_NETWORK} ${BBP_NETWORK}_${BBP_VERSION}-${DISTRO}${DEB_VER}_source.changes

