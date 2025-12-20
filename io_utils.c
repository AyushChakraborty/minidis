#include "io_utils.h"
#include <errno.h>
#include <assert.h>
#include <unistd.h>

int32_t read_full(int fd, char *buf, size_t n) {
    //to read n bytes from the fd into the buf
    errno = 0;
    while (n > 0) {
        int rv = read(fd, buf, n);

        if (rv < 0) {
            if (errno == EINTR) {   //if an interrupt to the syscall is seen, then continue
                continue;
            }
            return -1;
        }else if (rv == 0) {
            return -1;
        }

        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;    //move the buf ptr by the amt read
    }
    return 0;
}

int32_t write_all(int fd, char *buf, size_t n) {
    errno = 0;
    while (n > 0) {
        int rv = write(fd, buf, n);

        if (rv < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }else if (rv == 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}
