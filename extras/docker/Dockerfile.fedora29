# Dockerfile for testing the build of gluster-block based on Fedora 29

FROM fedora:29

ENV BUILDDIR=/build
RUN mkdir -p $BUILDDIR
WORKDIR $BUILDDIR

COPY . $BUILDDIR

# prepare the system
RUN true \
 && dnf -y update && dnf clean all \
 && true
RUN true \
 && dnf -y install \
           git autoconf automake gcc libtool make file rpcgen \
           glusterfs-api-devel libuuid-devel json-c-devel libtirpc-devel \
 && true

# build
RUN true \
 && ./autogen.sh \
 && ./configure \
 && make \
 && make check \
 && make install \
 && make clean \
 && true

