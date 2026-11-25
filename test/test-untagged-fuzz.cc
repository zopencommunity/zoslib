// Property-based / fuzz tests for the untagged-file read heuristic.
//
// zoslib is linked into virtually every zopen tool, so the heuristic's
// invariants must hold for ALL inputs, not only the corner cases a
// reviewer happens to construct. These tests hammer the decision with
// deterministic pseudo-random and exhaustive-boundary inputs:
//
//   1. Valid UTF-8 content (any mix of 1-4 byte characters, any size,
//      including every scan-window boundary alignment) is NEVER
//      converted.
//   2. 100%-valid CP1047 content that is not valid UTF-8 (any size,
//      including window boundaries and dangling lead bytes) is ALWAYS
//      converted.
//   3. Content containing NUL bytes is NEVER converted.
//   4. The heuristic never crashes and always restores the file
//      position (the caller can read the original bytes back when no
//      conversion was applied).
//
// All generators are seeded deterministically so failures reproduce.

#define ZOSLIB_OVERRIDE_CLIB 1

#include "zos.h"
#include "zos-char-util.h"
#include "gtest/gtest.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

// Deterministic pseudo-random generator (no dependence on libc rand()).
static unsigned long long g_state = 0x9E3779B97F4A7C15ULL;
static unsigned long long RandULong() {
  g_state += 0x9E3779B97F4A7C15ULL;
  unsigned long long z = g_state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
static size_t RandBelow(size_t n) { return (size_t)(RandULong() % n); }

// The 102 byte values that are valid CP1047 code points (from _tab_e).
static const unsigned char kValid1047[] = {
    0x05, 0x0B, 0x0C, 0x0D, 0x15, 0x16, 0x2F, 0x40, 0x4B, 0x4C, 0x4D, 0x4E,
    0x4F, 0x50, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x6B, 0x6C,
    0x6D, 0x6E, 0x6F, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x81, 0x82,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95,
    0x96, 0x97, 0x98, 0x99, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
    0xA9, 0xAD, 0xBD, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
    0xC9, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xE0,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xF0, 0xF1, 0xF2, 0xF3,
    0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9};

// Encode one code point as UTF-8; returns the byte count (0 if the
// code point is not encodable, i.e. a surrogate).
static size_t EncodeUtf8(unsigned int cp, unsigned char out[4]) {
  if (cp < 0x80) {
    out[0] = (unsigned char)cp;
    return 1;
  } else if (cp < 0x800) {
    out[0] = (unsigned char)(0xC0 | (cp >> 6));
    out[1] = (unsigned char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp >= 0xD800 && cp <= 0xDFFF) {
    return 0; // surrogates are not valid UTF-8
  } else if (cp < 0x10000) {
    out[0] = (unsigned char)(0xE0 | (cp >> 12));
    out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (unsigned char)(0x80 | (cp & 0x3F));
    return 3;
  } else if (cp <= 0x10FFFF) {
    out[0] = (unsigned char)(0xF0 | (cp >> 18));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
}

// Generate valid UTF-8 text of exactly size bytes (the generator is
// restarted for each size so the padding to an exact length is ASCII).
static void GenUtf8(std::vector<unsigned char> &out, size_t size,
                    size_t max_cp_shift) {
  out.clear();
  while (out.size() < size) {
    unsigned int cp;
    switch (RandBelow(5)) {
    case 0:  cp = (unsigned int)RandBelow(0x80); break;          // ASCII
    case 1:  cp = 0x80 + (unsigned int)RandBelow(0x7FF - 0x80); break;
    case 2:  cp = 0x800 + (unsigned int)RandBelow(0xD7FF - 0x800); break;
    case 3:  cp = 0xE000 + (unsigned int)RandBelow(0xFFFF - 0xE000); break;
    default: cp = 0x10000 + (unsigned int)RandBelow(0x10FFFF - 0x10000); break;
    }
    if (max_cp_shift == 0 && cp >= 0x80) cp &= 0x7F; // ASCII-only mode
    unsigned char enc[4];
    size_t n = EncodeUtf8(cp, enc);
    if (n == 0)
      continue;
    if (out.size() + n > size) {
      // pad the remainder with ASCII to hit the exact size
      while (out.size() < size)
        out.push_back((unsigned char)('a' + RandBelow(26)));
      break;
    }
    out.insert(out.end(), enc, enc + n);
  }
}

class UntaggedFuzzTest : public ::testing::Test {
protected:
  void SetUp() override {
    setenv("__UNTAGGED_READ_MODE", "AUTO", 1);
    __update_envar_settings("__UNTAGGED_READ_MODE");
    __set_ccsid_guess_buf_size(4096);
    temp_path = tmpnam(NULL);
    ASSERT_TRUE(temp_path != NULL);
  }
  void TearDown() override {
    __set_ccsid_guess_buf_size(4096);
    remove(temp_path);
  }

  // Returns the fd ccsid the hook decided on (0x10000+1047 = convert).
  // When expect_passthrough_content is non-NULL, also reads the file
  // back and verifies the original bytes are returned unchanged.
  long Decide(const unsigned char *data, size_t len,
              const unsigned char *passthrough_check = NULL) {
    remove(temp_path);
    int fd = open(temp_path, O_CREAT | O_WRONLY, 0777);
    if (fd < 0)
      return -1;
    __setfdccsid(fd, 0);
    const unsigned char *p = data;
    size_t off = 0;
    while (off < len) {
      ssize_t n = write(fd, p + off, len - off);
      if (n <= 0) {
        close(fd);
        return -1;
      }
      off += (size_t)n;
    }
    close(fd);
    fd = open(temp_path, O_RDONLY);
    if (fd < 0)
      return -1;
    long ccsid = __getfdccsid(fd);
    if (passthrough_check && ccsid == 0) {
      unsigned char *buf = (unsigned char *)malloc(len);
      if (buf) {
        ssize_t n = read(fd, buf, len);
        /* Only compare when the full buffer was read: a short or failed
         * read leaves uninitialized bytes that must not reach memcmp. */
        if (n == (ssize_t)len) {
          EXPECT_EQ(memcmp(buf, passthrough_check, len), 0)
              << "file position or content corrupted on passthrough";
        } else {
          ADD_FAILURE() << "short read (" << n << " of " << len
                        << " bytes) on passthrough check";
        }
        free(buf);
      }
    }
    close(fd);
    return ccsid;
  }

  const char *temp_path;
};

// Property 1: valid UTF-8 is never converted, across random sizes
// (including well past the 4096-byte scan window) and content mixes.
TEST_F(UntaggedFuzzTest, ValidUtf8NeverConvertedRandomSizes) {
  std::vector<unsigned char> data;
  for (int iter = 0; iter < 200; iter++) {
    size_t size = 1 + RandBelow(6000);
    GenUtf8(data, size, 1);
    long ccsid = Decide(data.data(), data.size());
    ASSERT_GE(ccsid, 0) << "iter " << iter << " size " << size;
    EXPECT_EQ(ccsid, 0) << "valid UTF-8 converted at iter " << iter
                        << " size " << size;
  }
}

// Property 1b: valid UTF-8 is never converted at every exact size
// around the scan-window boundary (4090..4110), so every possible
// multi-byte split alignment of the lookahead is exercised.
TEST_F(UntaggedFuzzTest, ValidUtf8NeverConvertedWindowBoundarySizes) {
  std::vector<unsigned char> data;
  for (size_t size = 4090; size <= 4110; size++) {
    for (int rep = 0; rep < 5; rep++) {
      GenUtf8(data, size, 1);
      long ccsid = Decide(data.data(), data.size());
      ASSERT_GE(ccsid, 0) << "size " << size;
      EXPECT_EQ(ccsid, 0) << "valid UTF-8 converted at size " << size;
    }
  }
}

// Property 1c: pure ASCII is never converted (any size).
TEST_F(UntaggedFuzzTest, AsciiNeverConverted) {
  std::vector<unsigned char> data;
  for (int iter = 0; iter < 100; iter++) {
    size_t size = 1 + RandBelow(6000);
    GenUtf8(data, size, 0);
    long ccsid = Decide(data.data(), data.size(), data.data());
    ASSERT_GE(ccsid, 0);
    EXPECT_EQ(ccsid, 0) << "ASCII converted at iter " << iter;
  }
}

// Property 2: 100%-valid CP1047 content that is not valid UTF-8 is
// always converted, across random sizes and window boundaries.
// The generator mixes all 102 valid code points, so the sample
// frequently contains UTF-8 lead/continuation-looking bytes.
TEST_F(UntaggedFuzzTest, FullValidEbcdicAlwaysConvertedRandomSizes) {
  std::vector<unsigned char> data;
  for (int iter = 0; iter < 200; iter++) {
    size_t size = 2 + RandBelow(6000);
    data.clear();
    for (size_t i = 0; i < size; i++)
      data.push_back(kValid1047[RandBelow(sizeof(kValid1047))]);
    // A continuation byte at offset 0 is always invalid UTF-8, so the
    // sample is guaranteed not to be classified ASCII; the remainder
    // stays random so lead/continuation-looking bytes still occur at
    // every position, including the scan-window boundary.
    data[0] = 0x85;
    long ccsid = Decide(data.data(), data.size());
    ASSERT_GE(ccsid, 0) << "iter " << iter << " size " << size;
    EXPECT_EQ(ccsid, 0x10000 + 1047) << "valid EBCDIC not converted at iter "
                                     << iter << " size " << size;
  }
}

// Property 2b: a lead-looking byte (X'C8') exactly at the last byte of
// the scan window, followed by a non-continuation byte, is always
// converted, for every file size past the boundary. (A file whose
// entire 4KB window is X'40' padding is ambiguous -- X'40' is valid
// ASCII '@' as well as EBCDIC space -- so the EBCDIC evidence must be
// inside the window for conversion to be required.)
TEST_F(UntaggedFuzzTest, EbcdicDanglingLeadWindowBoundaryAlwaysConverted) {
  for (size_t extra = 0; extra <= 13; extra++) {
    // 4095 spaces + C8 at offset 4095 (last window byte) + newline
    // + `extra` trailing spaces; file sizes 4097..4110.
    std::vector<unsigned char> data(4095, 0x40);
    data.push_back(0xC8);
    data.push_back(0x15);
    data.insert(data.end(), extra, 0x40);
    long ccsid = Decide(data.data(), data.size());
    ASSERT_GE(ccsid, 0) << "extra " << extra;
    EXPECT_EQ(ccsid, 0x10000 + 1047) << "EBCDIC not converted, extra "
                                     << extra;
  }
  // Whole file inside the window: same content shape, sizes 4090..4096.
  for (size_t size = 4090; size <= 4096; size++) {
    std::vector<unsigned char> data(size, 0x40);
    data[size - 2] = 0xC8;
    data[size - 1] = 0x15;
    long ccsid = Decide(data.data(), data.size());
    ASSERT_GE(ccsid, 0) << "size " << size;
    EXPECT_EQ(ccsid, 0x10000 + 1047) << "EBCDIC not converted at size "
                                     << size;
  }
}

// Property 3: NUL anywhere means no conversion, regardless of the
// rest of the content.
TEST_F(UntaggedFuzzTest, NulAnywhereNeverConverted) {
  std::vector<unsigned char> data;
  for (int iter = 0; iter < 200; iter++) {
    size_t size = 2 + RandBelow(6000);
    data.clear();
    data.resize(size);
    for (size_t i = 0; i < size; i++)
      data[i] = (unsigned char)RandULong();
    data[RandBelow(size)] = 0x00;
    long ccsid = Decide(data.data(), data.size());
    ASSERT_GE(ccsid, 0) << "iter " << iter;
    EXPECT_EQ(ccsid, 0) << "NUL-containing file converted at iter " << iter;
  }
}

// Property 4: arbitrary random bytes never crash the heuristic, and
// when no conversion is applied the original bytes are readable back
// (file position restored).
TEST_F(UntaggedFuzzTest, RandomBytesNoCrashPassthroughIntegrity) {
  std::vector<unsigned char> data;
  for (int iter = 0; iter < 300; iter++) {
    size_t size = 1 + RandBelow(6000);
    data.clear();
    data.resize(size);
    for (size_t i = 0; i < size; i++)
      data[i] = (unsigned char)RandULong();
    long ccsid = Decide(data.data(), data.size(), data.data());
    ASSERT_GE(ccsid, 0) << "heuristic failed at iter " << iter << " size "
                        << size;
    // Decision must be one of the two known outcomes.
    EXPECT_TRUE(ccsid == 0 || ccsid == 0x10000 + 1047)
        << "unexpected ccsid " << ccsid << " at iter " << iter;
  }
}

}  // namespace
