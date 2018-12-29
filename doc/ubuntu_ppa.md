Ubuntu Personal Packages (PPA)
====================================
This guide will show you how to build and publish Biblepay ubuntu packages into Launchpad.


Preparation
-----------

##Install debian build tools

    sudo apt install devscripts build-essential debhelper

##Install/import valid Launchpad PGP keys into your system

Build helper script
----------------------

- Move or copy ./contrib/ppa_build.sh into your user home folder (not a Biblepay root folder)

- Make it executable
    chmod +x ppa_build.sh
    
- Run it with the following parameters:

    ./ppa_build.sh [version] [[network]=mainnet] [[git_branch]=master] [[distro]=xenial] [[debian_version]=1]
    
For example

    ./ppa_build.sh 1.1.6.1 mainnet master bionic 1
    
Where 

version=biblepay version to package
network=mainnet or testnet, used to separate mainnet and testnet PPAs
git_branch=master or develop, used to refer to a Git branch build to download for source code
distro=xenial or bionic, ubuntu distro the package is built for
debian_version=to allow uploading the same source version several times in case of error in the previous ones.

Notes
----------------------

- If source changes, you will have to select a different version number (you can add letters at the end if you want to maintain the number)

- You have to prepare and upload different packages for each distro.

Example
    ./ppa_build.sh 1.1.6.1 mainnet master xenial 1
    ./ppa_build.sh 1.1.6.1 mainnet master bionic 1

