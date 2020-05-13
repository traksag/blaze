#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <mach/mach_time.h>

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static uint64_t current_tick;
static int server_sock;
static volatile sig_atomic_t got_sigint;

#define INITIAL_CONNECTION_IN_USE ((unsigned) (1 << 0))

enum initial_protocol_state {
    PROTOCOL_HANDSHAKE,
    PROTOCOL_AWAIT_STATUS_REQUEST,
    PROTOCOL_AWAIT_PING_REQUEST,
    PROTOCOL_AWAIT_HELLO,
};

typedef struct {
    int sock;
    unsigned flags;

    // Should be large enough to:
    //
    //  1. Receive a client intention (handshake) packet, status request and
    //     ping request packet all in one go and store them together in the
    //     buffer.
    //
    //  2. Receive a client intention packet and hello packet and store them
    //     together inside the receive buffer.
    // @TODO(traks) can maybe be a bit smaller
    unsigned char rec_buf[300];
    int rec_cursor;

    // @TODO(traks) figure out appropriate size
    unsigned char send_buf[600];
    int send_cursor;

    int protocol_state;
} initial_connection;

typedef struct {
    int32_t size;
    unsigned char * ptr;
} net_string;

static initial_connection initial_connections[32];
static int initial_connection_count;

static void
log(void * format, ...) {
    char msg[128];
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, sizeof msg, format, ap);
    va_end(ap);

    struct timespec now;
    char unknown_time[] = "?? ??? ???? ??:??:??.???";
    char time[25];
    if (clock_gettime(CLOCK_REALTIME, &now)) {
        // Time doesn't fit in time_t. Print out a bunch of question marks
        // instead of the correct time.
        memcpy(time, unknown_time, sizeof unknown_time);
    } else {
        struct tm local;
        if (!localtime_r(&now.tv_sec, &local)) {
            // Failed to convert time to broken-down time. Again, print out a
            // bunch of question marks instead of the current time.
            memcpy(time, unknown_time, sizeof unknown_time);
        } else {
            int milli = now.tv_nsec / 1000000;
            int second = local.tm_sec;
            int minute = local.tm_min;
            int hour = local.tm_hour;
            int day = local.tm_mday;
            int month = local.tm_mon;
            int year = local.tm_year + 1900;
            char * month_name = "???";
            switch (month) {
            case 0: month_name = "Jan"; break;
            case 1: month_name = "Feb"; break;
            case 2: month_name = "Mar"; break;
            case 3: month_name = "Apr"; break;
            case 4: month_name = "May"; break;
            case 5: month_name = "Jun"; break;
            case 6: month_name = "Jul"; break;
            case 7: month_name = "Aug"; break;
            case 8: month_name = "Sep"; break;
            case 9: month_name = "Oct"; break;
            case 10: month_name = "Nov"; break;
            case 11: month_name = "Dec"; break;
            }
            snprintf(time, sizeof time, "%02d %s %04d %02d:%02d:%02d.%03d", day,
                    month_name, year, hour, minute, second, milli);
        }
    }

    // @TODO(traks) buffer messages instead of going through printf each time?
    printf("%s  %s\n", time, msg);
}

static void
log_errno(void * format) {
    char error_msg[64] = {0};
    strerror_r(errno, error_msg, sizeof error_msg);
    log(format, error_msg);
}

static void
handle_sigint(int sig) {
    got_sigint = 1;
}

static int32_t
read_varint(unsigned char * buf, int limit, int * cursor, int * error) {
    uint32_t in = 0;
    unsigned char * data = buf + *cursor;
    int remaining = limit - *cursor;
    // first decode the first 1-4 bytes
    int end = MIN(remaining, 4);
    int i;
    for (i = 0; i < end; i++) {
        unsigned char b = data[i];
        in |= (uint32_t) (b & 0x7f) << (i * 7);

        if ((b & 0x80) == 0) {
            // final byte marker found
            goto exit;
        }
    }

    // The first bytes were decoded. If we reached the end of the buffer, it is
    // missing the final 5th byte.
    if (remaining < 5) {
        *error = 1;
        return 0;
    }
    unsigned char final = data[4];
    in |= (uint_least32_t) (final & 0xf) << 28;

exit:
    *cursor += i + 1;
    if (in <= 0x7fffffff) {
        return in;
    } else {
        return (int32_t) (in - 0x80000000) + (-0x7fffffff - 1);
    }
}

static void
write_varint(unsigned char * buf, int limit, int * cursor, int * error,
        int32_t val) {
    uint32_t out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (uint32_t) (val + 0x7fffffff + 1) + 0x80000000;
    }

    int remaining = limit - *cursor;
    unsigned char * data = buf + *cursor;

    // write each block of 7 bits until no more are necessary
    int i = 0;
    for (;;) {
        if (i >= remaining) {
            *error = 1;
            return;
        }
        if (out <= 0x7f) {
            data[i] = out;
            *cursor += i + 1;
            break;
        } else {
            data[i] = 0x80 | (out & 0x7f);
            out >>= 7;
            i++;
        }
    }
}

static int
varint_size(int32_t val)
{
    // @TODO(traks) The current implementation of this function can probably be
    // optimised quite a bit. Maybe use an instruction to get the highest set
    // bit, then divide by 7.

    uint32_t x;
    // convert to two's complement representation
    if (val >= 0) {
        x = val;
    } else {
        x = (uint32_t) (val + 0x7fffffff + 1) + 0x80000000;
    }

    int res = 1;
    while (x > 0x7f) {
        x >>= 7;
        res++;
    }
    return res;
}

static uint16_t
read_ushort(unsigned char * buf, int limit, int * cursor, int * error) {
    if (limit - *cursor < 2) {
        *error = 1;
        return 0;
    }

    uint16_t res = 0;
    res |= (uint16_t) buf[*cursor] << 8;
    res |= (uint16_t) buf[*cursor + 1];
    *cursor += 2;
    return res;
}

static uint64_t
read_ulong(unsigned char * buf, int limit, int * cursor, int * error) {
    if (limit - *cursor < 8) {
        *error = 1;
        return 0;
    }

    uint64_t res = 0;
    res |= (uint64_t) buf[*cursor] << 54;
    res |= (uint64_t) buf[*cursor + 1] << 48;
    res |= (uint64_t) buf[*cursor + 2] << 40;
    res |= (uint64_t) buf[*cursor + 3] << 32;
    res |= (uint64_t) buf[*cursor + 4] << 24;
    res |= (uint64_t) buf[*cursor + 5] << 16;
    res |= (uint64_t) buf[*cursor + 6] << 8;
    res |= (uint64_t) buf[*cursor + 7];
    *cursor += 8;
    return res;
}

static void
write_ulong(unsigned char * buf, int limit, int * cursor, int * error,
        uint64_t val) {
    if (limit - *cursor < 8) {
        *error = 1;
        return;
    }

    buf[*cursor] = (val >> 56) & 0xff;
    buf[*cursor + 1] = (val >> 48) & 0xff;
    buf[*cursor + 2] = (val >> 40) & 0xff;
    buf[*cursor + 3] = (val >> 32) & 0xff;
    buf[*cursor + 4] = (val >> 24) & 0xff;
    buf[*cursor + 5] = (val >> 16) & 0xff;
    buf[*cursor + 6] = (val >> 8) & 0xff;
    buf[*cursor + 7] = (val >> 0) & 0xff;
    *cursor += 8;
}

static net_string
read_string(unsigned char * buf, int limit, int * cursor, int * error) {
    int32_t size = read_varint(buf, limit, cursor, error);
    net_string res = {0};
    if (size < 0 || size > limit - *cursor) {
        *error = 1;
        return res;
    }

    res.size = size;
    res.ptr = buf + *cursor;
    *cursor += size;
    return res;
}

static void
server_tick(void) {
    for (;;) {
        int accepted = accept(server_sock, NULL, NULL);
        if (accepted == -1) {
            break;
        }

        if (initial_connection_count == ARRAY_SIZE(initial_connections)) {
            close(accepted);
            continue;
        }

        int flags = fcntl(accepted, F_GETFL, 0);

        if (flags == -1) {
            close(accepted);
            continue;
        }

        if (fcntl(accepted, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(accepted);
            continue;
        }

        // @TODO(traks) should we lower the receive and send buffer sizes? For
        // hanshakes/status/login they don't need to be as large as the default
        // (probably around 200KB).

        initial_connection * new_connection;
        for (int i = 0; ; i++) {
            new_connection = initial_connections + i;
            if ((new_connection->flags & INITIAL_CONNECTION_IN_USE) == 0) {
                break;
            }
        }

        *new_connection = (initial_connection) {0};
        new_connection->sock = accepted;
        new_connection->flags |= INITIAL_CONNECTION_IN_USE;
        initial_connection_count++;
    }

    for (int i = 0; i < ARRAY_SIZE(initial_connections); i++) {
        initial_connection * init_con = initial_connections + i;
        if ((init_con->flags & INITIAL_CONNECTION_IN_USE) == 0) {
            continue;
        }

        int sock = init_con->sock;
        ssize_t rec_size = recv(sock, init_con->rec_buf + init_con->rec_cursor,
                sizeof init_con->rec_buf - init_con->rec_cursor, 0);

        if (rec_size == 0) {
            close(sock);
            init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
            initial_connection_count--;
        } else if (rec_size == -1) {
            // EAGAIN means no data received
            if (errno != EAGAIN) {
                log_errno("Couldn't receive protocol data: %s");
                close(sock);
                init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
                initial_connection_count--;
            }
        } else {
            init_con->rec_cursor += rec_size;

            int cursor = 0;
            int error = 0;

            for (;;) {
                int32_t packet_size = read_varint(init_con->rec_buf,
                        init_con->rec_cursor, &cursor, &error);

                if (error != 0) {
                    // packet size not fully received yet
                    break;
                }
                if (packet_size <= 0 || packet_size > sizeof init_con->rec_buf) {
                    close(sock);
                    init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
                    initial_connection_count--;
                    break;
                }
                if (packet_size > init_con->rec_cursor) {
                    // packet not fully received yet
                    break;
                }

                int packet_start = cursor;
                int32_t packet_id = read_varint(init_con->rec_buf,
                        init_con->rec_cursor, &cursor, &error);

                switch (init_con->protocol_state) {
                case PROTOCOL_HANDSHAKE: {
                    if (packet_id != 0) {
                        error = 1;
                    }

                    int32_t protocol_version = read_varint(init_con->rec_buf,
                            init_con->rec_cursor, &cursor, &error);
                    net_string address = read_string(init_con->rec_buf,
                            init_con->rec_cursor, &cursor, &error);
                    uint16_t port = read_ushort(init_con->rec_buf,
                            init_con->rec_cursor, &cursor, &error);
                    int32_t next_state = read_varint(init_con->rec_buf,
                            init_con->rec_cursor, &cursor, &error);

                    if (next_state == 1) {
                        init_con->protocol_state = PROTOCOL_AWAIT_STATUS_REQUEST;
                    } else if (next_state == 2) {
                        init_con->protocol_state = PROTOCOL_AWAIT_HELLO;
                    } else {
                        error = 1;
                    }
                    break;
                }
                case PROTOCOL_AWAIT_STATUS_REQUEST: {
                    if (packet_id != 0) {
                        error = 1;
                    }

                    char response[] =
                            "{"
                            "  \"version\": {"
                            "    \"name\": \"1.15.2\","
                            "    \"protocol\": 578"
                            "  },"
                            "  \"players\": {"
                            "    \"max\": 100,"
                            "    \"online\": 0,"
                            "    \"sample\": []"
                            "  },"
                            "  \"description\": {"
                            "    \"text\": \"Running Blaze\""
                            "  }"
                            "}";
                    int out_size = varint_size(0)
                            + varint_size(sizeof response - 1)
                            + (sizeof response - 1);
                    write_varint(init_con->send_buf, sizeof init_con->send_buf,
                            &init_con->send_cursor, &error, out_size);
                    write_varint(init_con->send_buf, sizeof init_con->send_buf,
                            &init_con->send_cursor, &error, 0);
                    write_varint(init_con->send_buf, sizeof init_con->send_buf,
                            &init_con->send_cursor, &error, sizeof response - 1);
                    memcpy(init_con->send_buf + init_con->send_cursor,
                            response, sizeof response - 1);
                    init_con->send_cursor += sizeof response - 1;

                    init_con->protocol_state = PROTOCOL_AWAIT_PING_REQUEST;
                    break;
                }
                case PROTOCOL_AWAIT_PING_REQUEST: {
                    if (packet_id != 1) {
                        error = 1;
                    }

                    uint64_t payload = read_ulong(init_con->rec_buf,
                            init_con->rec_cursor, &cursor, &error);

                    int out_size = varint_size(1) + 8;
                    write_varint(init_con->send_buf, sizeof init_con->send_buf,
                            &init_con->send_cursor, &error, out_size);
                    write_varint(init_con->send_buf, sizeof init_con->send_buf,
                            &init_con->send_cursor, &error, 1);
                    write_ulong(init_con->send_buf, sizeof init_con->send_buf,
                            &init_con->send_cursor, &error, payload);
                    break;
                }
                case PROTOCOL_AWAIT_HELLO: {
                    if (packet_id != 0) {
                        error = 1;
                    }

                    net_string username = read_string(init_con->rec_buf,
                            init_con->rec_cursor, &cursor, &error);
                    log("Username is '%.*s'", username.size, username.ptr);

                    // @TODO(traks) online mode
                    // @TODO(traks) enable compression
                    // @TODO(traks) move to gameplay connection and spawn the
                    // player and so on

                    error = 1;
                    break;
                }
                default:
                    log("Unknown protocol state %d", init_con->protocol_state);
                    error = 1;
                    break;
                }

                if (packet_size != cursor - packet_start) {
                    error = 1;
                }

                if (error != 0) {
                    log("Protocol error occurred");
                    close(sock);
                    init_con->flags = 0;
                    initial_connection_count--;
                }
            }

            memmove(init_con->rec_buf, init_con->rec_buf + init_con->rec_cursor,
                    sizeof init_con->rec_buf - init_con->rec_cursor);
            init_con->rec_cursor -= cursor;
        }
    }

    for (int i = 0; i < ARRAY_SIZE(initial_connections); i++) {
        initial_connection * init_con = initial_connections + i;
        if ((init_con->flags & INITIAL_CONNECTION_IN_USE) == 0) {
            continue;
        }

        int sock = init_con->sock;
        ssize_t send_size = send(sock, init_con->send_buf,
                init_con->send_cursor, 0);

        if (send_size == -1) {
            // EAGAIN means no data sent
            if (errno != EAGAIN) {
                log_errno("Couldn't send protocol data: %s");
                close(sock);
                init_con->flags = 0;
                initial_connection_count--;
            }
        } else {
            memmove(init_con->send_buf, init_con->send_buf + send_size,
                    init_con->send_cursor - send_size);
            init_con->send_cursor -= send_size;
        }
    }

    current_tick++;
}

int
main(void) {
    log("Running Blaze");

    // Ignore SIGPIPE so the server doesn't crash (by getting signals) if a
    // client decides to abruptly close its end of the connection.
    signal(SIGPIPE, SIG_IGN);

    // @TODO(traks) ctrl+c is useful for debugging if the program ends up inside
    // an infinite loop
    // signal(SIGINT, handle_sigint);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(25565),
        .sin_addr = {
            .s_addr = htonl(0x7f000001)
        }
    };

    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock == -1) {
        log_errno("Failed to create socket: %s");
        exit(1);
    }
    if (server_sock >= FD_SETSIZE) {
        // can't select on this socket
        log("Socket is FD_SETSIZE or higher");
        exit(1);
    }

    struct sockaddr * addr_ptr = (struct sockaddr *) &server_addr;

    int yes = 1;
    if (setsockopt(server_sock, SOL_SOCKET,
            SO_REUSEADDR, &yes, sizeof yes) == -1) {
        log_errno("Failed to set sock opt: %s");
        exit(1);
    }

    // @TODO(traks) non-blocking connect? Also note that connect will finish
    // asynchronously if it has been interrupted by a signal.
    if (bind(server_sock, addr_ptr, sizeof server_addr) == -1) {
        log_errno("Can't bind to address: %s");
        exit(1);
    }

    if (listen(server_sock, 16) == -1) {
        log_errno("Can't listen: %s");
        exit(1);
    }

    int flags = fcntl(server_sock, F_GETFL, 0);

    if (flags == -1) {
        log_errno("Can't get socket flags: %s");
        exit(1);
    }

    if (fcntl(server_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_errno("Can't set socket flags: %s");
        exit(1);
    }

    log("Bound to address");

    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);

    for (;;) {
        uint64_t start_time = mach_absolute_time();

        server_tick();

        uint64_t end_time = mach_absolute_time();
        uint64_t elapsed_micros = (end_time - start_time)
                * timebase_info.numer / timebase_info.denom / 1000;

        if (got_sigint) {
            log("Interrupted");
            break;
        }

        if (elapsed_micros < 50000) {
            usleep(50000 - elapsed_micros);
        }
    }

    log("Goodbye!");
    return 0;
}
