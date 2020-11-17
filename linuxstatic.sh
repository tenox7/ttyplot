#!/bin/bash -xe
# overcomplicated method of building static version of ttyplot for linux
docker rmi -f static
cat <<EOF | docker build -t static  -f- .
FROM ubuntu:xenial
RUN apt-get update -qq
RUN apt-get install -qq -o=Dpkg::Use-Pty=0 build-essential file git curl vim-tiny
RUN echo "\n\
rm -rf /ttyplot* /ncurses*\n\
git clone https://github.com/tenox7/ttyplot.git\n\
curl -Ls ftp://ftp.invisible-island.net/ncurses/ncurses.tar.gz | tar xzf -\n\
cd ncurses*\n\
./configure -q --with-terminfo-dirs=/etc/terminfo:/lib/terminfo:/usr/lib/terminfo:/usr/share/terminfo\n\
make -s\n\
mv include/ lib/libncurses.a /ttyplot\n\
cd /ttyplot\n\
gcc -Iinclude ttyplot.c -static -o /out/ttyplot-amd64-linux libncurses.a\n\
strip /out/ttyplot-amd64-linux\n\
file /out/ttyplot-amd64-linux\n\
" > /build.sh
ENTRYPOINT ["/bin/bash", "/build.sh"]
EOF
docker run -v ${PWD}:/out -it --rm static
