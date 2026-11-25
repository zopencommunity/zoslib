/*
 * Delivery driver for the subprocess stdin-probe tests.
 *
 * Deliberately NOT linked against zoslib: its open() is the plain LE
 * open, so the file descriptor handed to the probe helper arrives
 * untagged with no conversion armed -- a clean stdin delivery that
 * lets the helper's own process initialization (the code under test)
 * make every decision.
 *
 * Usage: stdin_probe_driver <file> <offset> <helper> [helper args...]
 */
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
  if (argc < 4)
    return 2;
  int fd = open(argv[1], O_RDONLY);
  if (fd < 0)
    return 126;
  long offset = atol(argv[2]);
  if (offset > 0 && lseek(fd, offset, SEEK_SET) < 0)
    return 125;
  dup2(fd, 0);
  execv(argv[3], &argv[3]);
  return 127;
}
