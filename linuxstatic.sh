#!/bin/bash -xe
# overcomplicated method of building static version of ttyplot for linux
docker rmi -f static
cat <<EOF | docker build -t static  -f- .
FROM ubuntu:xenial
RUN apt-get update -qq
RUN apt-get install -qq -o=Dpkg::Use-Pty=0 build-essential file curl
RUN echo "\n\
rm -rf /ttyplot* /ncurses*\n\
curl -Ls ftp://ftp.invisible-island.net/ncurses/ncurses.tar.gz | tar xzf -\n\
cd ncurses*\n\
./configure -q --with-terminfo-dirs=/etc/terminfo:/lib/terminfo:/usr/lib/terminfo:/usr/share/terminfo\n\
make -s -j$(nproc)\n\
curl -LOs https://raw.githubusercontent.com/tenox7/ttyplot/master/ttyplot.c\n\
gcc -Iinclude ttyplot.c -static -o /out/ttyplot-amd64-linux lib/libncurses.a\n\
strip /out/ttyplot-amd64-linux\n\
file /out/ttyplot-amd64-linux\n\
" > /build.sh
ENTRYPOINT ["/bin/bash", "/build.sh"]
EOF
docker run -v ${PWD}:/out -it --rm static
