#include <assert.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include "hashtable.h"

//below macro is to find the ptr to the key within
//a struct Entry node
#define container_of(ptr, T, member) \
    ((T *)((char *)ptr - offsetof(T, member)))

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
   size_t remaining = conn->outgoing_status.data_end - conn->outgoing_status.data_begin;
    //if all data written, update the readiness intention
    if (remaining == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

//here is the req pattern:
// | nstr | len | str1 | len | str2 | .....
// where nstr actually contains the net size of this req
// and each req/cmd is a sequence of strings specified by the
// len str pairs

static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
    if (cur + 4 > end) {
        return false;
    }
    memcpy(&out, cur, 4);
    cur += 4;
    return true;
}

static bool read_str(const uint8_t *&cur, const uint8_t *end, uint32_t &len, std::string &out) {
    if (cur + len > end) {
        return false;
    }
    out.assign(cur, cur+len);
    cur += len;
    return true;
}

const size_t max_args = 200 * 1000;

static int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out) {
    const uint8_t *end = data + size;
    uint32_t nstr = 0;
    if (!read_u32(data, end, nstr)) {
        return -1;
    }
    if (nstr > max_args) {
        return -1;
    }

    while (out.size() < nstr) {
        uint32_t len = 0;
        if (!read_u32(data, end, len)) {
            return -1;
        }
        out.push_back(std::string());
        if (!read_str(data, end, len, out.back())) {
            return -1;
        }
    }
    if (data != end) {
        return -1;   //garbage trailing values present
    }
    return 0;
}

//format of response
// | status | data ... |
enum {
    RES_OK = 0,
    RES_ERR = 1, //error
    RES_NX = 2,    //key not found
};

//error code for TAG_ERR
enum {
    ERR_UNKNOWN = 1,    // unknown command
    ERR_TOO_BIG = 2,    // response too big
};

//data types of serialised data
enum {
    TAG_NIL = 0,    // nil
    TAG_ERR = 1,    // error code + msg
    TAG_STR = 2,    // string
    TAG_INT = 3,    // int64
    TAG_DBL = 4,    // double
    TAG_ARR = 5,    // array
};

static void buf_append_u8(std::vector<uint8_t> &buf, struct Buffer &status, uint8_t data) {
    buf_append(&buf, &status, &data, 1);
}

static void buf_append_u32(std::vector<uint8_t> &buf, struct Buffer &status, uint32_t data) {
    buf_append(&buf, &status, (const uint8_t *)&data, 4);
}

static void buf_append_i64(std::vector<uint8_t> &buf, struct Buffer &status, int64_t data) {
    buf_append(&buf, &status, (const uint8_t *)&data, 8);
}

//double precision float, takes 8 bytes
static void buf_append_dbl(std::vector<uint8_t> &buf, struct Buffer &status, double data) {
    buf_append(&buf, &status, (const uint8_t *)&data, 1);
}


//append serialised data types, serilaised type is of:
//| tag | len | value |
static void out_nil(std::vector<uint8_t> &buf, struct Buffer &status) {
    buf_append_u8(buf, status, TAG_NIL);
}

static void out_str(std::vector<uint8_t> &buf, struct Buffer &status, const char *str_data, size_t size) {
    buf_append_u8(buf, status, TAG_STR);   //tag
    buf_append_u32(buf, status, (uint32_t)size);     //len, allocation of 4B to it
    buf_append(&buf, &status, (const uint8_t *)str_data, size);   //data, variable allocation based on size
}

static void out_int(std::vector<uint8_t> &buf, struct Buffer &status, int64_t int_val) {
    buf_append_u8(buf, status, TAG_INT);
    buf_append_i64(buf, status, int_val);
}

static void out_dbl(std::vector<uint8_t> &buf, struct Buffer &status, double dbl_val) {
    buf_append_u8(buf, status, TAG_DBL);
    buf_append_dbl(buf, status, dbl_val);
}

static void out_err(std::vector<uint8_t> &buf, struct Buffer &status, uint32_t code, const std::string &msg) {
    buf_append_u8(buf, status, TAG_ERR);
    buf_append_u32(buf, status, code);
    buf_append_u32(buf, status, (uint32_t)msg.size());
    buf_append(&buf, &status, (const uint8_t *)msg.data(), msg.size());
}

static void out_arr(std::vector<uint8_t> &buf, struct Buffer &status, uint32_t n) {
    buf_append_u8(buf, status, TAG_ARR);
    buf_append_u32(buf, status, n);
}

//kv store
static struct {
    HMap db;
} store;

struct Entry {
    struct HNode node;    //hashtable node
    std::string key;
    std::string val;
};

//eq function for the struct Entry node
static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

//needed to store the hash of the key for each node
static uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i=0; i<len; i++) {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

static void do_get(std::vector<std::string> &cmd, std::vector<uint8_t> &buf, struct Buffer &status) {
    //dummy node needed to search, fill the info from cmd
    //to this dummy node to then do the search in the hashtable
    Entry datanode;
    datanode.key.swap(cmd[1]);
    datanode.node.hcode = str_hash((uint8_t *)datanode.key.data(), datanode.key.size());

    HNode *node = hm_lookup(&store.db, &datanode.node, &entry_eq);
    if (!node) {
        return out_nil(buf, status);
    }

    //copy the data to out
    const std::string &val = container_of(node, Entry, node)->val;
    return out_str(buf, status, val.data(), val.size());
}

static void do_set(std::vector<std::string> &cmd, std::vector<uint8_t> &buf, struct Buffer &status) {
    //again dummy Entry for lookup
    Entry datanode;
    datanode.key.swap(cmd[1]);
    datanode.node.hcode = str_hash((uint8_t *)datanode.key.data(), datanode.key.size());

    HNode *node = hm_lookup(&store.db, &datanode.node, &entry_eq);
    if (node) {
        //node found, meaning key was already present
        container_of(node, Entry, node)->val.swap(cmd[2]);
    }else {
        //not found so allocate and insert a new pair
        Entry *entry = new Entry();
        entry->key.swap(datanode.key);
        entry->node.hcode = datanode.node.hcode;
        entry->val.swap(cmd[2]);
        hm_insert(&store.db, &entry->node);
    }
    return out_nil(buf, status);
}

static void do_del(std::vector<std::string> &cmd,std::vector<uint8_t> &buf, struct Buffer &status) {
    Entry datanode;
    datanode.key.swap(cmd[1]);
    datanode.node.hcode = str_hash((uint8_t *)datanode.key.data(), datanode.key.size());

    HNode *node = hm_delete(&store.db, &datanode.node, &entry_eq);
    if (node) {
        //deallcate the key value pair then
        delete container_of(node, Entry, node);
    }
    return out_int(buf, status, node ? 1 : 0);
}

struct ScanContext {
    std::vector<uint8_t> *buf;
    struct Buffer *status;
};

static bool cb_keys(HNode *node, void *arg) {
    ScanContext *ctx = (ScanContext *)arg;
    const std::string &key = container_of(node, Entry, node)->key;
    out_str(*ctx->buf, *ctx->status, key.data(), key.size());
    return true;
}

static void do_keys(std::vector<std::string> &, std::vector<uint8_t> &buf, struct Buffer &status) {
    out_arr(buf, status, (uint32_t)hm_size(&store.db));
    ScanContext ctx = {&buf, &status};
    hm_foreach(&store.db, &cb_keys, (void*)&ctx);
}

static void do_request(std::vector<std::string> &cmd, std::vector<uint8_t> &buf, struct Buffer &status) {
    if (cmd.size() == 2 && cmd[0] == "get") {
        return do_get(cmd, buf, status);
    }else if (cmd.size() == 3 && cmd[0] == "set") {
        return do_set(cmd, buf, status);
    }else if (cmd.size() == 2 && cmd[0] == "del") {
        return do_del(cmd, buf, status);
    }else if (cmd.size() == 1 && cmd[0] == "keys") {
        return do_keys(cmd, buf, status);
    }
    else {
        //unrecognised command
        return out_err(buf, status, ERR_UNKNOWN, "unknown command");
    }
}

static void response_begin(std::vector<uint8_t> &buf, struct Buffer &status, size_t *header) {
    *header = status.data_end;
    buf_append_u32(buf, status, 0);
}

static size_t response_size(struct Buffer &status, size_t header) {
    return status.data_end - header - 4;
}

static void response_end(std::vector<uint8_t> &buf, struct Buffer &status, size_t header) {
    size_t msg_size = response_size(status, header);
    if (msg_size > max_msg_size) {
        status.data_end = header + 4; //move the data_end ptr back
        out_err(buf, status, ERR_TOO_BIG, "response is too big");
        msg_size = response_size(status, header);
    }
    uint32_t len = (uint32_t)msg_size;
    memcpy(&buf[header], &len, 4); //write the header at the front
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

    //application logic here
    std::vector<std::string> cmd;
    if (parse_req(req, len, cmd) < 0) {
        msg("bad request");
        conn->want_close = true;
        return false;
    }

    // Response resp;
    size_t header_pos = 0;
    response_begin(conn->outgoing, conn->outgoing_status, &header_pos);
    do_request(cmd, conn->outgoing, conn->outgoing_status);
    response_end(conn->outgoing, conn->outgoing_status, header_pos);

    //remove the req message
    buf_consume(&conn->incoming_status, 4 + len);
    return true;
}


static void handle_read(Conn *conn) {
    size_t data_len_incoming = conn->incoming_status.data_end - conn->incoming_status.data_begin;

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

    size_t curr_outgoing_len = conn->outgoing_status.data_end - conn->outgoing_status.data_begin;

    //update readiness intention
    if (curr_outgoing_len > 0) {    //has a response
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
