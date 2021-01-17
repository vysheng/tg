FROM ubuntu AS build
RUN apt update && DEBIAN_FRONTEND=noninteractive apt -y install libreadline-dev libconfig-dev libssl-dev lua5.2 liblua5.2-dev libevent-dev libjansson-dev libpython2-dev make zlib1g-dev libgcrypt20-dev git
WORKDIR /data
RUN git clone --recursive https://github.com/vysheng/tg.git && cd tg
RUN cd tg && ./configure --disable-openssl --prefix=/usr CFLAGS="$CFLAGS -w" && make

FROM ubuntu
RUN apt update && DEBIAN_FRONTEND=noninteractive apt -y install libevent-dev libjansson-dev libconfig-dev libreadline-dev liblua5.2-dev
WORKDIR /data
COPY --from=build /data/tg/bin/telegram-cli /usr/local/bin/telegram-cli

ENTRYPOINT ["telegram-cli"]
