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
const size_t k_max_msg = 32 << 20;

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
        buf += n;
    }
    return 0;
}

static int32_t send_req(int fd, const uint8_t *text, size_t len) {
    if (len > k_max_msg) {
        return -1;
    }
    std::vector<uint8_t> wbuf;
    wbuf.insert(wbuf.end(), (const uint8_t *)&len, (const uint8_t *)&len + 4);
    wbuf.insert(wbuf.end(), text, text + len);
    return write_all(fd, wbuf.data(), wbuf.size());
}

static int32_t read_res(int fd) {
    //4 bytes header first
    std::vector<uint8_t> rbuf;
    rbuf.resize(4);
    errno = 0;
    return read_full(fd, &rbuf[0], 4);
}

int main() {
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

//   //send multiple reqs to the server
//   printf("sending message: hello1 to server\n");
//   int32_t err = query(fd, "hello1");
//   if (err) {
//       goto DONE;
//   }
//   printf("sending message: hello2 to server\n");
//   err = query(fd, "hello2");
//   if (err) {
//       goto DONE;
//   }
//   printf("sending message: hello3 to server\n");
//   err = query(fd, "hello3");
//   if (err) {
//       goto DONE;
//   }

// DONE:
//     close(fd);

    //send pipelines reqs
    std::vector<std::string> query_list = {"hello1", "hello2", "hello3", std::string(k_max_msg, 'z'), "hello5"};

    //the teo for loops below represent the pipelined req,
    //so it sends req in sucession, wihtout waiting for res
    for (auto &s : query_list) {
        int32_t err = send_req(fd, (uint8_t *)s.data(), s.size());
        if (err) {
            close(fd);
        }
    }

    for (size_t i=0; i<query_list.size(); ++i) {
        int32_t err = read_res(fd);
        if (err) {
            close(fd);
        }
    }

    return 0;
}
