#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s: %s\n", err, msg, strerror(errno));
  abort();
}

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void action(int fd) {
  char rbuf[64] = {};
  ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);

  if (n < 0) {
    msg("read error");
    return;
  }
  fprintf(stderr, "client says: %s\n", rbuf);

  char wbuf[] = "message from server";
  write(fd, wbuf, strlen(wbuf));
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

    action(connfd);
    close(connfd);
  }
}
