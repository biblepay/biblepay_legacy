FROM debian:stretch
LABEL maintainer="Biblepay Developers <dev@biblepay.org>"
LABEL description="Dockerised BiblepayCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
