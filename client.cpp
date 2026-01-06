#include <arpa/inet.h>
#include <cstdint>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <assert.h>
#include "io_utils.h"

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s: %s\n", err, msg, strerror(errno));
  abort();
}

const size_t max_buffer_size = 4096;
// const size_t k_max_msg = 32 << 20;

int32_t query(int fd, const char *text) {
    //write the text to the fd
    uint32_t len = (uint32_t)strlen(text);
    if (len > max_buffer_size) {
        return -1;
    }

    char wbuf[4 + sizeof(text)];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    int32_t err = write_all(fd, wbuf, 4 + len);
    if (err < 0) {
        return err;
    }

    //also read the response from the server

    char rbuf[4 + max_buffer_size];
    errno = 0;
    err = read_full(fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }

    memcpy(&len, rbuf, 4);
    if (len > max_buffer_size) {
        msg("too long");
        return -1;
    }

    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    printf("server says: %.*s\n", len, &rbuf[4]);
    return 0;

}

// static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
//     buf.insert(buf.end(), data, data + len);
// }

static int32_t write_all(int fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t read_full(int fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t send_req(int fd, const std::vector<std::string> cmd) {
    //sends: | len header (TCP) | nstr (application header) | len | str1 | len | str2 | .....

    uint32_t len = 4;
    for (const std::string &s : cmd) {
        len += 4 + s.size();  //to add the 4B of len for each string in the cmd
    }
    if (len > max_buffer_size) {
        return -1;
    }
    
    uint8_t wbuf[4 + max_buffer_size];
    memcpy(&wbuf[0], &len, 4);   //length of the entire command
    //along with the prefix lens of each string, needed as TCP is used
    
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);   //this is the actual nstr size as defined
    //by the application logic, just contains the cmd size alone
    
    size_t cur = 8;
    //pipelined req
    for (const std::string &s : cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

static int32_t read_res(int fd) {
    //reads: | len header (TCP) | status | response |
    //the status and response make up the body here
    //4 bytes header first
    uint8_t rbuf[4 + max_buffer_size];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        }else {
            msg("read error");
        }
        return err;
    }
    
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > max_buffer_size) {
        msg("message too long");
        return -1;
    }
    
    //reply body
    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read error");
        return err;
    }
    
    //print the res
    uint32_t rescode = 0;
    if (len < 4) {
        msg("bad response");
        return -1;
    }
    memcpy(&rescode, &rbuf[4], 4);
    printf("server says: [%u] %.*s\n", rescode, len-4, &rbuf[8]);
    return 0;
}

int main(int argc, char **argv) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // connect to the server

  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
    die("could not connect");
  }

    std::vector<std::string> cmd;
    for (int i=1; i<argc; ++i) {
        cmd.push_back(argv[i]);
    }
    
    int32_t err = send_req(fd, cmd);
    if (err) {
        close(fd);
    }
    err = read_res(fd);
    if (err) {
        close(fd);
    }
    return 0;
}
