# Builds the .deb for an Ubuntu release like the release workflow, but unsigned, then installs
# it and runs Callgrind on it. The final stage holds only the package, named as its release
# asset, for `docker build --output`.
ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS build
ARG UBUNTU_VERSION
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -q \
    && apt-get install -y -q build-essential devscripts debhelper dh-make \
    && apt-get install -y -q debhelper-compat gdb mpi-default-dev pkgconf docbook docbook-xsl \
        docbook-xml xsltproc

WORKDIR /build/src
COPY . .
RUN debuild --no-tgz-check -nc -us -uc >/build/debuild.log 2>&1 \
    || { tail -n 50 /build/debuild.log; exit 1; }

RUN deb=$(ls /build/valgrind_*.deb) \
    && version=$(dpkg-deb -f "$deb" Version) \
    && mkdir /out \
    && cp "$deb" "/out/valgrind_${version#*:}_ubuntu-${UBUNTU_VERSION}_$(dpkg-deb -f "$deb" Architecture).deb"
RUN apt-get install -y -q /out/*.deb \
    && valgrind --version \
    && valgrind --tool=callgrind --cycle-estimation=yes --callgrind-out-file=/dev/null /bin/true

FROM scratch
COPY --from=build /out/ /
