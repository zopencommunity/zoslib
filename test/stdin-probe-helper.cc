/*
 * Helper for the subprocess stdin-probe integration tests.
 *
 * This program links zoslib, so its process initialization (including
 * the untagged-stdin content probe) runs before main(). The test
 * harness forks, redirects a file onto fd 0 (optionally at a nonzero
 * offset and with environment overrides), and execs this helper; the
 * helper reports the stdin fd ccsid chosen by the probe, whether
 * conversion is armed on the fd, and the first bytes it reads.
 */
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include "zos.h" /* __getfdccsid: also forces the zoslib import */

int main() {
  char out[512];
  unsigned char buf[64];
  /*
   * Query the fd tag before reading. This mirrors how real consumers
   * behave (any stdio use or first fstat has the same effect): LE
   * materializes the tag-driven conversion armed during process
   * initialization only on the descriptor's first access from the
   * running program. A raw read(2) with no prior access observes the
   * armed conversion only after such a query.
   */
  int ccsid_before = __getfdccsid(0);
  ssize_t n = read(0, buf, sizeof(buf));
  int len = snprintf(out, sizeof(out), "CCSID=%d N=%zd BYTES=",
                     ccsid_before, n);
  for (ssize_t i = 0; i < n && i < 16; i++)
    len += snprintf(out + len, sizeof(out) - len, "%02x ", buf[i]);
  snprintf(out + len, sizeof(out) - len, "\n");
  write(1, out, strlen(out));
  return 0;
}
