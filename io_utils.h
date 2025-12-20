#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t write_all(int fd, char *buf, size_t n);
int32_t read_full(int fd, char *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif
