#ifndef UTIL_H_
#define UTIL_H_

#include <stdint.h>

void enable_debug_log(void);
void set_debug_logfile(const char *filename);
void debug_log(const char *fmt, ...);

const char *sha1_sig(uint8_t *buf, uint32_t size);
uint32_t get_rand(uint32_t min, uint32_t max);

#endif
