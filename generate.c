#include <stdio.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "tl-tl.h"

static char buf[1 << 20];
int buf_size;
int *buf_ptr = (int *)buf;
int *buf_end;

int verbosity;

int get_int (void) {
  assert (buf_ptr < buf_end);
  return *(buf_ptr ++);
}

long long get_long (void) {
  assert (buf_ptr + 1 < buf_end);
  long long r = *(long long *)buf_ptr;
  buf_ptr += 2;
  return r;
}

char *get_string (int *len) {
  int l = *(unsigned char *)buf_ptr;
  assert (l != 0xff);
  
  char *res;
  int tlen = 0;
  if (l == 0xfe) {
    l = ((unsigned)get_int ()) >> 8;
    res = (char *)buf_ptr;
    tlen = l;
  } else {
    res = ((char *)buf_ptr) + 1;
    tlen = 1 + l;
  }
  *len = l;
  
  tlen += ((-tlen) & 3);
  assert (!(tlen & 3));

  buf_ptr += tlen / 4;
  assert (buf_ptr <= buf_end);
  
  return res;
}


int parse_type (void) {
  assert (get_int () == TLS_TYPE);
  int name = get_int ();
  int l;
  char *name = get_string (&l);
  int flags = get_int ();
  int arity = get_int ();
  long long params_types = get_long ();

  printf ("int fetch_type_%s (struct paramed_type *T) {\n");
  printf ("  if (arity) { assert (T); }\n");

  printf ("\n");

  return 0;
}

int parse_tlo_file (void) {
  buf_end = buf_ptr + (buf_size / 4);
  assert (get_int () == TLS_SCHEMA_V2);

  get_int (); // version
  get_int (); // date

  int i;

  int types_num = get_int ();

  for (i = 0; i < types_num; i++) {
    if (parse_type () < 0) { return -1; }
  }


  
  return 0;
}

void usage (void) {
  printf ("usage: generate [-v] [-h] <tlo-file>\n"
       );
  exit (2);
}

void logprintf (const char *format, ...) __attribute__ ((format (printf, 1, 2)));
void logprintf (const char *format __attribute__ ((unused)), ...) {
}

void hexdump (int *in_ptr, int *in_end) {
  int *ptr = in_ptr;
  while (ptr < in_end) { printf (" %08x", *(ptr ++)); }
  printf ("\n");
}

#ifdef HAVE_EXECINFO_H
void print_backtrace (void) {
  void *buffer[255];
  const int calls = backtrace (buffer, sizeof (buffer) / sizeof (void *));
  backtrace_symbols_fd (buffer, calls, 1);
}
#else
void print_backtrace (void) {
  if (write (1, "No libexec. Backtrace disabled\n", 32) < 0) {
    // Sad thing
  }
}
#endif

void sig_segv_handler (int signum __attribute__ ((unused))) {
  if (write (1, "SIGSEGV received\n", 18) < 0) { 
    // Sad thing
  }
  print_backtrace ();
  exit (EXIT_FAILURE);
}

void sig_abrt_handler (int signum __attribute__ ((unused))) {
  if (write (1, "SIGABRT received\n", 18) < 0) { 
    // Sad thing
  }
  print_backtrace ();
  exit (EXIT_FAILURE);
}

int main (int argc, char **argv) {
  signal (SIGSEGV, sig_segv_handler);
  signal (SIGABRT, sig_abrt_handler);
  int i;
  while ((i = getopt (argc, argv, "vh")) != -1) {
    switch (i) {
    case 'h':
      usage ();
      return 2;
    case 'v':
      verbosity++;
      break;
    }
  }

  if (argc != optind + 1) {
    usage ();
  }

  int fd = open (argv[optind], O_RDONLY);
  if (fd < 0) {
    fprintf (stderr, "Can not open file '%s'. Error %m\n", argv[optind]);
    exit (1);
  }
  buf_size = read (fd, buf, (1 << 20));
  if (fd == (1 << 20)) {
    fprintf (stderr, "Too big tlo file\n");
    exit (2);
  }
  return parse_tlo_file ();
}
