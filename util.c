#include <err.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#include "util.h"

static int debug_log_enabled = 0;
static int debug_log_fd = 2;  /* by default write log to stderr */

void enable_debug_log(void) {
  debug_log_enabled = 1;
}

void set_debug_logfile(const char *filename) {
  debug_log_fd = open(filename, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR);
  if (debug_log_fd == -1)
    err(1, "failed to open log file %s", filename);
}

void debug_log(const char *fmt, ...) {
  if (!debug_log_enabled)
    return;

  va_list args;
  va_start(args, fmt);
  vdprintf(debug_log_fd, fmt, args);
  va_end(args);
  dprintf(debug_log_fd, "\n");
}

const char *sha1_sig(uint8_t *buf, uint32_t size) {
  static char sig[80];
  uint8_t obuf[20];

  SHA1(buf, size, obuf);
  for (int i = 0; i < 15; ++i) {
    char *p = (char *)sig + i * 5;
    sprintf(p, "0x%02x ", obuf[i]);
  }
  return sig;
}

uint32_t get_rand(uint32_t min, uint32_t max) {
  uint32_t v;
  int rc = RAND_bytes((uint8_t *)&v, sizeof(v));
  assert(rc);

  v = (uint32_t)(v/(UINT32_MAX/(max - min + 1))) + min;
  if (v == max+1)
    v = max;
  return v;
}
