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

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s: %s\n", err, msg, strerror(errno));
  abort();
}

const size_t max_buffer_size = 4096;
// const size_t k_max_msg = 32 << 20;


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

enum {
    TAG_NIL = 0,    // nil
    TAG_ERR = 1,    // error code + msg
    TAG_STR = 2,    // string
    TAG_INT = 3,    // int64
    TAG_DBL = 4,    // double
    TAG_ARR = 5,    // array 
};

static int32_t print_response(const uint8_t *data, size_t size) {
    if (size < 1) {
        msg("bad response");
        return -1;
    }
    switch (data[0]) {
        case TAG_NIL:
            printf("(nil)\n");
            return 1;
            
        case TAG_ERR:
            //1B is len of tag, 4B len of code, 4B len of length of err message
            //rest of the response is the error message itself, as denoted by data[1+8]
            if (size < 1 + 8) {
                msg("bad response");
                return -1;
            }
            {
                int32_t code = 0;
                uint32_t len = 0;
                memcpy(&code, &data[1], 4);
                memcpy(&len, &data[1 + 4], 4);
                if (size < 1 + 8 + len) {
                    msg("bad response");
                    return -1;
                }
                printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
                return 1 + 8 + len;
            }
            
        case TAG_STR:
            //1B is len of tag, 4B len of the string value, rest of 
            //it the string itself, as denoted by data[1+4]
            if (size < 1 + 4) {
                msg("bad response");
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);     //4 bytes allocated for string value
                if (size < 1 + 4 + len) {
                    msg("bad response");
                    return -1;
                }
                printf("(str) %.*s\n", len, &data[1+4]);
                return 1+4+len;
            } 
        
        case TAG_INT:
            //1B len of tag, 8B len of int value itself
            if (size < 1+8) {
                msg("bad response");
                return -1;
            }
            {
                int64_t val = 0;
                memcpy(&val, &data[1], 8);   //8 bytes allocated for i64 value
                printf("(int) %lld\n", val);
                return 1+8;
            }
        
        case TAG_DBL:
            //1B len of tag, 8B len of double value itself
            if (size < 1+8) {
                msg("bad response");
                return -1;
            }
            {
                double val = 0;
                memcpy(&val, &data[1], 8);   //8 bytes allocated for double value
                printf("(int) %g\n", val);
                return 1+8;
            }
        
        case TAG_ARR:
            if (size < 1+4) {
                msg("bad response");
                return -1;
            }
            {
                uint32_t len = 0;
                memcpy(&len, &data[1], 4);
                printf("(arr) len=%u\n", len);
                
                size_t arr_bytes = 1+4;
                for (uint32_t i=0; i<len; ++i) {
                    int32_t rv = print_response(&data[arr_bytes], size-arr_bytes);
                    if (rv < 0) {
                        return rv;
                    }
                    arr_bytes += (size_t)rv;
                }
                printf("(arr) end\n");
                return (int32_t)arr_bytes;
            }
        
        default:
            msg("bad respose");
            return -1;
    }
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
    
    int32_t rv = print_response((uint8_t *)&rbuf[4], len);
    if (rv > 0 && (uint32_t)rv != len) {
        msg("bad response (trailing garbage)");
        rv = -1;
    }
    return rv;
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
