#include <assert.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include "io_utils.h"

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s: %s\n", err, msg, strerror(errno));
  abort();
}

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void msg_errno(const char *msg) {
    fprintf(stderr, "[errno:%d] %s\n", errno, msg);
}

const size_t max_msg_size = 32 << 20;
const size_t buf_size = 64 << 10;  //64KB

static void fd_set_nb(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }
    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fnctl error");
    }
}

struct Buffer {
    size_t buffer_begin;
    size_t buffer_end;
    size_t data_begin;
    size_t data_end;
};

struct Conn {
    int fd = -1;
    //below fields are all applications intentions with this fd
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    std::vector<uint8_t> incoming;
    struct Buffer incoming_status;
    std::vector<uint8_t> outgoing;
    struct Buffer outgoing_status;

    Conn(): incoming(buf_size), outgoing(buf_size) {
        incoming_status.buffer_begin = 0;
        incoming_status.buffer_end = buf_size-1;
        incoming_status.data_begin = 0;
        incoming_status.data_end = 0;

        outgoing_status.buffer_begin = 0;
        outgoing_status.buffer_end = buf_size-1;
        outgoing_status.data_begin = 0;
        outgoing_status.data_end = 0;
    }
};

static Conn *handle_accept(int fd) {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
    if (connfd < 0) {
        msg_errno("accept() error");
        return NULL;
    }
    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    fd_set_nb(connfd);

    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true;
    return conn;
}

static void buf_append(std::vector<uint8_t> *buf, struct Buffer *status, const uint8_t *data, size_t len) {
    size_t available_space = status->buffer_end - status->data_end;
    assert(available_space >= len);
    memcpy(buf->data() + status->data_end, data, len);
    status->data_end += len;
}

static void buf_consume(struct Buffer *status, size_t n) {
    assert(status->data_begin + n <= status->buffer_end);
    status->data_begin += n;

    //case where the buff is basically empty, since the data begin ptr and the data end ptr are the same
    if (status->data_begin == status->data_end) {
        status->data_begin = 0;
        status->data_end = 0;
        //this is reclaiming the space
    }
}

static void handle_write(Conn *conn) {
    // assert(conn->outgoing.size() > 0);
    size_t data_len = conn->outgoing_status.data_end - conn->outgoing_status.data_begin;

    //if nothing more to write to socket
    if (data_len == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }

    ssize_t rv = write(conn->fd, conn->outgoing.data() + conn->outgoing_status.data_begin, data_len);
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;
        return;
    }

    //remove written from ongoing
   buf_consume(&conn->outgoing_status, (size_t)rv);

    //if all data written, update the readiness intention
    if (data_len == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static bool try_one_request(Conn *conn) {
    //parse message header, which is the protocol
    size_t data_len = conn->incoming_status.data_end - conn->incoming_status.data_begin;

    if (data_len < 4) {
        return false;
    }
    uint32_t len = 0;

    memcpy(&len, conn->incoming.data() + conn->incoming_status.data_begin, 4);
    if (len > max_msg_size) {
        msg("message too long");
        conn->want_close = true;
        return false;
    }

    if (4 + len > data_len) {
        return false;
    }

    const uint8_t *req = &conn->incoming[4];

    //dummy application logic here
    printf("client says: len:%d data:%.*s\n", len, len < 100 ? len : 100, req);

    //generate echo response
    buf_append(&conn->outgoing, &conn->outgoing_status, (const uint8_t *)&len, 4);
    buf_append(&conn->outgoing, &conn->outgoing_status, req, len);

    //remove the req message
    buf_consume(&conn->incoming_status, 4 + len);
    return true;
}


static void handle_read(Conn *conn) {
    size_t data_len_incoming = conn->incoming_status.data_end - conn->incoming_status.data_begin;
    size_t data_len_outgoing = conn->outgoing_status.data_end - conn->outgoing_status.data_begin;

    uint8_t buf[buf_size];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv < 0 && errno == EAGAIN) {
        return;       //socket not ready
    }
    //incase of IO error
    if (rv < 0) {
        msg_errno("read() error");
        conn->want_close = true;  //application wants to close now
        return;
    }

    //handle EOF
    if (rv == 0) {
        if (data_len_incoming == 0) {
            msg("client closed");
        }else {
            msg("unexpected EOF");
        }
        conn->want_close = true ;
        return;
    }

    buf_append(&conn->incoming, &conn->incoming_status, buf, (size_t)rv);

    //parse reqs and generate responses
    while (try_one_request(conn)) {}     //in a while loop to handle pipelined reqs from client

    //update readiness intention
    if (data_len_outgoing > 0) {    //has a response
        conn->want_read = false;
        conn->want_write = true;
        //try to write now without waiting for the next iteration
        return handle_write(conn);
    }
}

//handle_read(), handle_write(), handle_accept() are the application logic
int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
      die("could not open socket");
  }

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

  //set listen fd to nonblocking mode
  fd_set_nb(fd);

  //listen
  rv = listen(fd, SOMAXCONN);
  if (rv) {
      die("listen failed");
  }

  //map of all client connections, keyed by fd
  std::vector<Conn *> fd_to_conn;

  //event loop
  std::vector<struct pollfd> poll_args;

  while (true) {
      poll_args.clear();

      //put listening socket in first pos
      struct pollfd pfd = {fd, POLLIN, 0};
      poll_args.push_back(pfd);

      //handling the rest connection sockets
      for (Conn *conn : fd_to_conn) {
          if (!conn) {
              continue;
          }

          struct pollfd pfd = {conn->fd, POLLERR, 0};
          if (conn->want_read) {
              pfd.events |= POLLIN;
          }if (conn->want_write) {
              pfd.events |= POLLOUT;
          }
          poll_args.push_back(pfd);
      }

      //wait for readiness
      int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
      if (rv < 0 && errno == EINTR) {
          continue;
      }if (rv < 0) {
          die("poll");
      }

      //handle listening socket
      if (poll_args[0].revents) {
          if (Conn *conn = handle_accept(fd)) {
              if (fd_to_conn.size() <= (size_t)conn->fd) {
                  fd_to_conn.resize(conn->fd + 1);
              }
              assert(!fd_to_conn[conn->fd]);
              fd_to_conn[conn->fd] = conn;
          }
      }

      //handle connection sockets
      for (size_t i=1; i<poll_args.size(); ++i) {
          uint32_t ready = poll_args[i].revents;
          if (ready == 0) {
              continue;       //fd not ready, so continue
          }

          Conn *conn = fd_to_conn[poll_args[i].fd];
          if (ready & POLLIN) {
              assert(conn->want_read);
              handle_read(conn);
          }if (ready & POLLOUT) {
              assert(conn->want_write);
              handle_write(conn);
          }

          //close socket in case of socket error or application logic (handled by conn->want_close)
          if ((ready & POLLERR) || conn-> want_close) {
              (void)close(conn->fd);
              fd_to_conn[conn->fd] = NULL;
              delete conn;
          }
      }
  }
  return 0;
}
