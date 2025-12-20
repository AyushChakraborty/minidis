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
#include "io_utils.h"

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s: %s\n", err, msg, strerror(errno));
  abort();
}

const size_t max_buffer_size = 4096;

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

  //send multiple reqs to the server
  printf("sending message: hello1 to server\n");
  int32_t err = query(fd, "hello1");
  if (err) {
      goto DONE;
  }
  printf("sending message: hello2 to server\n");
  err = query(fd, "hello2");
  if (err) {
      goto DONE;
  }
  printf("sending message: hello3 to server\n");
  err = query(fd, "hello3");
  if (err) {
      goto DONE;
  }
  
DONE:
    close(fd);
    return 0;
}
