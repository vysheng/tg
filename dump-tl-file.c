#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include "auto.h"

int *tgl_in_ptr, *tgl_in_end;

int tgl_packet_buffer[0];
int *tgl_packet_ptr;

char *tgl_strdup (char *s) {
  return strdup (s);
}

void tgl_out_cstring (const char *str, long len) {}
char *tglf_extf_fetch (struct paramed_type *T);

#define LEN (1 << 28)
static int x[LEN / 4];
int main (int argc, char **argv) {
  int i;
  int dump_binlog = 0;
  while ((i = getopt (argc, argv, "b")) != -1) {
    switch (i) {
    case 'b':
      dump_binlog = 1;
      break;
    default: 
      printf ("unknown option '%c'\n", (char)i);
      exit (1);
    }
  }
  if (!dump_binlog) {
    exit (1);
  }
  if (optind + 1 != argc) {
    exit (1);
  }
  int fd = open (argv[optind], O_RDONLY);
  if (fd < 0) {
    perror ("open");
    exit (1);
  }
  int r = read (fd, x, LEN);
  if (r <= 0) {
    perror ("read");
    exit (1);
  }
  if (r == LEN) {
    printf ("Too long file\n");
    exit (1);
  }
  assert (!(r & 3));
  tgl_in_ptr = x;
  tgl_in_end = x + (r / 4);
  while (tgl_in_ptr < tgl_in_end) {
    if (dump_binlog) {
      char *R = tglf_extf_fetch (TYPE_TO_PARAM(binlog_update));
      if (!R) {
        printf ("Can not fetch\n");
        exit (1);
      }
      printf ("%s\n", R);
    } 
  }
  return 0;
}
