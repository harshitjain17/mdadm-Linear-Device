#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <err.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "util.h"
#include "tester.h"
#include "net.h"

#define TESTER_ARGUMENTS "hw:s:"
#define USAGE                                               \
  "USAGE: test [-h] [-w workload-file] [-s cache_size] \n"  \
  "\n"                                                      \
  "where:\n"                                                \
  "    -h - help mode (display this message)\n"             \
  "\n"                                                      \

int run_workload(char *workload, int cache_size);

int main(int argc, char *argv[])
{
  int ch, cache_size = 0;
  char *workload = NULL;

  while ((ch = getopt(argc, argv, TESTER_ARGUMENTS)) != -1) {
    switch (ch) {
      case 'h':
        fprintf(stderr, USAGE);
        return 0;
      case 's':
        cache_size = atoi(optarg);
        break;
      case 'w':
        workload = optarg;
        break;
      default:
        fprintf(stderr, "Unknown command line option (%c), aborting.\n", ch);
        return -1;
    }
  }

  if (!workload) {
    fprintf(stderr, USAGE);
    return -1;
  }

  if (!jbod_connect(JBOD_SERVER, JBOD_PORT))
    return -1;
  
  run_workload(workload, cache_size);
  jbod_disconnect();

  return 0;
}

int equals(const char *s1, const char *s2) {
  return strncmp(s1, s2, strlen(s2)) == 0;
}

static uint32_t encode_op(jbod_cmd_t cmd, int disk_num, int block_num) {
  assert(cmd >= 0 && cmd < JBOD_NUM_CMDS);
  assert(block_num >= 0 && block_num < JBOD_NUM_BLOCKS_PER_DISK);

  uint32_t op = 0;
  op |= cmd << 26;
  op |= disk_num << 22;
  op |= block_num;

  return op;
}

int run_workload(char *workload, int cache_size) {
  char line[256], cmd[32];
  uint8_t buf[MAX_IO_SIZE];
  uint32_t addr, len, ch;
  int rc;

  memset(buf, 0, MAX_IO_SIZE);

  FILE *f = fopen(workload, "r");
  if (!f)
    err(1, "Cannot open workload file %s", workload);

  if (cache_size) {
    rc = cache_create(cache_size);
    if (rc != 1)
      errx(1, "Failed to create cache.");
  }

  int line_num = 0;
  while (fgets(line, 256, f)) {
    ++line_num;
    line[strlen(line)-1] = '\0';
    if (equals(line, "MOUNT")) {
      rc = mdadm_mount();
    } else if (equals(line, "UNMOUNT")) {
      rc = mdadm_unmount();
    } else if (equals(line, "SIGNALL")) {
      for (int i = 0; i < JBOD_NUM_DISKS; ++i)
        for (int j = 0; j < JBOD_NUM_BLOCKS_PER_DISK; ++j) {
          uint8_t b[JBOD_BLOCK_SIZE];
          jbod_client_operation(encode_op(JBOD_SIGN_BLOCK, i, j), b);
          fprintf(stdout, "%s", b);
        }
    } else {
      if (sscanf(line, "%7s %7u %4u %3u", cmd, &addr, &len, &ch) != 4)
        errx(1, "Failed to parse command: [%s\n], aborting.", line);
      if (equals(cmd, "READ")) {
        rc = mdadm_read(addr, len, buf);
      } else if (equals(cmd, "WRITE")) {
        memset(buf, ch, len);
        rc = mdadm_write(addr, len, buf);
      } else {
        errx(1, "Unknown command [%s] on line %d, aborting.", line, line_num);
      }
    }

    if (rc == -1)
      errx(1, "tester failed when processing command [%s] on line %d", line, line_num);
  }
  fclose(f);

  if (cache_size)
    cache_destroy();

  jbod_print_cost();
  cache_print_hit_rate();

  return 0;
}
