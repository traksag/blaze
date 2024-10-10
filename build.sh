profile=0
slow=0
assert=0

TRACY_VER="0.11.0"
CFLAGS=""
LIBS="-lz -lm -lpthread"

if [ $slow == 0 ]; then
    CFLAGS+=" -flto -O3 -march=native"
fi

if [ $assert == 0 ]; then
    CFLAGS+=" -DNDEBUG"
fi

if [ $profile == 0 ]; then
    cc $CFLAGS -o blaze src/*.c $LIBS
elif [ $profile == 1 ]; then
    if [ ! -e "lib/tracy-${TRACY_VER}" ]; then
        # download Tracy if not present
        mkdir -p lib
        wget -O lib/tracy.zip https://github.com/wolfpld/tracy/archive/v${TRACY_VER}.zip
        unzip -q lib/tracy.zip -d lib
        rm lib/tracy.zip
    fi

    LIBS+=" -ldl"

    if [[ "$OSTYPE" == darwin* ]]; then
        # No lib atomic on macOS
        LIBS+=""
    else
        LIBS+=" -latomic"
    fi

    # enable Tracy
    CFLAGS+=" -DPROFILE -DTRACY_ENABLE"
    # don't allow remote connections and only log data when profiler connected
    CFLAGS+=" -DTRACY_ONLY_LOCALHOST -DTRACY_ON_DEMAND"

    cc $CFLAGS -g -c src/*.c -I lib/tracy-${TRACY_VER}/public
    c++ $CFLAGS -g -std=c++11 -c lib/tracy-${TRACY_VER}/public/TracyClient.cpp
    c++ $CFLAGS -g -o blaze *.o $LIBS
fi
