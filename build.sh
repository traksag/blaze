# set to 1 for profiling
debug=0
# set to 1 for slower program, but faster compilation
slow=0

set -x

CFLAGS=""

if [ $slow == 0 ]; then
    CFLAGS+="-flto -O3 -march=native"
fi

if [ $debug == 0 ]; then
    cc $CFLAGS -o blaze src/*.c -lz -lm
elif [ $debug == 1 ]; then
    if [ ! -e "lib/tracy" ]; then
        # download Tracy if not present
        TRACY_VER="0.7.6"

        mkdir -p lib
        wget -O lib/tracy.zip https://github.com/wolfpld/tracy/archive/v${TRACY_VER}.zip
        unzip -q lib/tracy.zip -d lib
        mv lib/tracy-${TRACY_VER} lib/tracy
        rm lib/tracy.zip
    fi

    cc $CFLAGS -g -c src/*.c -I lib -DPROFILE -DTRACY_ENABLE
    c++ $CFLAGS -g -std=c++11 -c lib/tracy/TracyClient.cpp -DTRACY_ENABLE
    c++ $CFLAGS -g -o blaze *.o -lz -lm
fi
