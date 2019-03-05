#!/usr/bin/env bash
 
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

DOCKER_IMAGE=${DOCKER_IMAGE:-biblepay/biblepayd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/biblepayd docker/bin/
cp $BUILD_DIR/src/biblepay-cli docker/bin/
cp $BUILD_DIR/src/biblepay-tx docker/bin/
strip docker/bin/biblepayd
strip docker/bin/biblepay-cli
strip docker/bin/biblepay-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
