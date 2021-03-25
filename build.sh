profile=0
slow=0
assert=0

CFLAGS=""
LIBS="-lz -lm"

if [ $slow == 0 ]; then
    CFLAGS+=" -flto -O3 -march=native"
fi

if [ $assert == 0 ]; then
    CFLAGS+=" -DNDEBUG"
fi

if [ $profile == 0 ]; then
    cc $CFLAGS -o blaze src/*.c $LIBS
elif [ $profile == 1 ]; then
    if [ ! -e "lib/tracy" ]; then
        # download Tracy if not present
        TRACY_VER="0.7.6"

        mkdir -p lib
        wget -O lib/tracy.zip https://github.com/wolfpld/tracy/archive/v${TRACY_VER}.zip
        unzip -q lib/tracy.zip -d lib
        mv lib/tracy-${TRACY_VER} lib/tracy
        rm lib/tracy.zip
    fi

    LIBS+=" -lpthread -ldl"

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

    cc $CFLAGS -g -c src/*.c -I lib
    c++ $CFLAGS -g -std=c++11 -c lib/tracy/TracyClient.cpp
    c++ $CFLAGS -g -o blaze *.o $LIBS
fi
