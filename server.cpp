#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include "io_utils.h"

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s: %s\n", err, msg, strerror(errno));
  abort();
}

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

const size_t max_buffer_size = 4096;       //in B

static int32_t one_request(int connfd) {
    char rbuf[4 + max_buffer_size];    //4B for the header
    errno = 0;
    
    int32_t err = read_full(connfd, rbuf, 4);    //read the 4B header
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }
    
    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > max_buffer_size) {
        msg("length longer than buffer size");
        return -1;
    }
    
    //processing req body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return -1;
    }
    printf("client says: %.*s\n", len, &rbuf[4]);
    
    //send the reply, just reply "shaka from server"
    const char reply[] = "shaka from server";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    
    return write_all(connfd, wbuf, 4 + len);
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234); // as a rule, use ports above 1024 only, as the
                               // the ones below are privileged
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
      die("bind()");
  }
  
  rv = listen(fd, SOMAXCONN); // size of queue is SOMAXCONN or 4096
  printf("listening to reqs...\n");
  while (true) {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    printf("connection estab\n");
    if (connfd < 0) {
      continue;
    }

    while (true) {
        int32_t err = one_request(connfd);
        if (err) {
            break;
        }
    }
    close(connfd);
  }
}
