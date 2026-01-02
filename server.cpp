#include <assert.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
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

const size_t max_buffer_size = 4096;       //in B
const size_t max_msg_size = 32 << 20;

// static int32_t one_request(int connfd) {
//     char rbuf[4 + max_buffer_size];    //4B for the header
//     errno = 0;
    
//     int32_t err = read_full(connfd, rbuf, 4);    //read the 4B header
//     if (err) {
//         msg(errno == 0 ? "EOF" : "read() error");
//         return err;
//     }
    
//     uint32_t len = 0;
//     memcpy(&len, rbuf, 4);
//     if (len > max_buffer_size) {
//         msg("length longer than buffer size");
//         return -1;
//     }
    
//     //processing req body
//     err = read_full(connfd, &rbuf[4], len);
//     if (err) {
//         msg("read() error");
//         return -1;
//     }
//     printf("client says: %.*s\n", len, &rbuf[4]);
    
//     //send the reply, just reply "shaka from server"
//     const char reply[] = "shaka from server";
//     char wbuf[4 + sizeof(reply)];
//     len = (uint32_t)strlen(reply);
    
//     memcpy(wbuf, &len, 4);
//     memcpy(&wbuf[4], reply, len);
    
//     return write_all(connfd, wbuf, 4 + len);
// }


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

struct Conn {
    int fd = -1;
    //below fields are all applications intentions with this fd
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
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

static bool try_one_request(Conn *conn) {
    //parse message header, which is the protocol
    if (conn->incoming.size() < 4) {
        return false;
    }
    uint32_t len = 0;
    
    memcpy(&len, conn->incoming.data(), 4);
    if (len > max_msg_size) {
        msg("message too long");
        conn->want_close = true;
        return false;
    }
    
    if (4 + len > conn->incoming.size()) {
        return false;
    }
    
    const uint8_t *req = &conn->incoming[4];
    
    //dummy application logic here
    printf("client says: len:%d data:%.*s\n", len, len < 100 ? len : 100, req);
    
    //generate echo response
    conn->outgoing.insert(conn->outgoing.end(), (const uint8_t *)&len, (const uint8_t *)&len + 4);
    conn->outgoing.insert(conn->outgoing.end(), req, req + len);
    
    //remove the req message
    conn->incoming.erase(conn->incoming.begin(), conn->incoming.begin() + 4 + len);
    return true;
}

static void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
    if (rv < 0 && errno == EAGAIN) {
        return;
    }
    if (rv < 0) {
        msg_errno("write() error");
        conn->want_close = true;
        return;
    }
    
    //remove written from ongoing
    conn->outgoing.erase(conn->outgoing.begin(), conn->outgoing.begin() + (size_t)rv);
    
    //if all data written, update the readiness intention
    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

static void handle_read(Conn *conn) {
    uint8_t buf[64 * 1024];
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
        if (conn->incoming.size() == 0) {
            msg("client closed");
        }else {
            msg("unexpected EOF");
        }
        conn->want_close = true ;
        return;
    }
    
    conn->incoming.insert(conn->incoming.end(), buf, buf + (size_t)rv);
    
    //parse reqs and generate responses
    while (try_one_request(conn)) {}
    
    //update readiness intention
    if (conn->outgoing.size() > 0) {    //has a response
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
