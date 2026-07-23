FROM spectranext/sdk-alpine:latest AS sdk

FROM alpine:latest AS client

COPY --from=sdk /sdk /sdk

ENV SPECTRANEXT_SDK_PATH=/sdk

RUN apk update && apk add --no-cache python3 python3-dev cmake git build-base python3 py3-pip py3-setuptools \
    build-base perl zlib-dev m4 ragel bash
RUN pip3 install Pillow pyYAML requests --break-system-packages

RUN mkdir /sandbox && mkdir /sandbox/tnfsd/ && mkdir /sandbox/tnfsd/data && \
    mkdir /sandbox/client/ && mkdir /sandbox/server && mkdir /build

# common
ADD common /build/common
ADD common /sandbox/common

# server
COPY server/CMakeLists.txt /build/server/CMakeLists.txt
COPY server/include /build/server/include
COPY server/src /build/server/src
COPY server/target /build/server/target
COPY server/uthash /build/server/uthash
COPY server/pages /build/server/pages
COPY server/data /build/server/data
COPY server/Z80 /build/server/Z80
COPY server/dnslib /build/server/dnslib
COPY server/Zeta /build/server/Zeta

# client
COPY client/CMakeLists.txt /build/client/CMakeLists.txt
COPY client/include /build/client/include
COPY client/data /build/client/data
COPY client/gui /build/client/gui
COPY client/src /build/client/src
COPY client/modules /build/client/modules
COPY client/tools /build/client/tools

ENV CLIENT_MODULES_BIN="/build/client/build"
ENV DOCKER_BUILD="1"
ENV PATH="/sdk/bin:/sdk/z88dk/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
ADD client/config/zx.cfg /sdk/z88dk/share/z88dk/lib/config/zx.cfg
RUN mkdir /sandbox/server/pages

RUN mkdir /build/client/build && cd /build/client/build && \
    /sdk/source.sh && cmake -DCMAKE_BUILD_TYPE=Release -DZCCTARGET=zx -DCMAKE_TOOLCHAIN_FILE=/sdk/z88dk/share/z88dk/cmake/Toolchain-zcc.cmake .. && \
    make client && make all_modules && cp /build/client/build/client.bin /sandbox/server/pages/client.bin && \
    cd /build/server/data && ./make_tiles.sh && \
    cp -r /build/server/pages/* /sandbox/server/pages && \
    mkdir /build/server/build && cd /build/server/build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make server && \
    cp /build/server/build/server /sandbox/server/server

FROM alpine:latest

RUN apk update && apk add --no-cache python3 py3-pip py3-setuptools busybox-extras gcc libstdc++ && pip3 install --break-system-packages Pillow pyYAML requests qrcode uvicorn fastapi==0.99.0 jinja2 python-multipart websockets

COPY --from=client /sandbox /sandbox

COPY server/python /sandbox/server/python

ENV REPORT_URL=https://unbound-api.spectranext.net
ENV REPORT_ADDRESS="ub1.spectranext.net"
ENV REPORT_ICON=FF818181818181FF
ENV REPORT_ICON_COLOR=4
ENV REPORT_NAME="Unbound Demo Server"
ENV REPORT_PORT=13390
ENV ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:strict_string_checks=1:check_initialization_order=1
ENV UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

WORKDIR /sandbox/server
CMD /sandbox/server/server ${ZX_SERVER_ARGS}
