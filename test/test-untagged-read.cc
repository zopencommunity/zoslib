// Tests for the untagged-file read heuristic (__file_needs_conversion_init)
// and the __UNTAGGED_READ_MODE / __CCSIDGUESSBUFSIZE controls.
//
// Covers the EBCDIC/ASCII decision matrix (including files with bytes
// that are invalid in CCSID 1047, e.g. X'66'), end-to-end read-back,
// binary corner cases, and the mode/scan-size controls.
//
// Enable CLIB overrides so that open() runs the zoslib hooks.
#define ZOSLIB_OVERRIDE_CLIB 1

#include "zos.h"
#include "zos-char-util.h"
#include "gtest/gtest.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// "echo asdf\n" in IBM-1047 (EBCDIC).
static const unsigned char kCleanEbcdic[] = {
    0x85, 0x83, 0x88, 0x96, 0x40, 0x81, 0xa2, 0x84, 0x86, 0x15};

// Same as kCleanEbcdic, but EBCDIC 'f' (X'86') replaced with X'66',
// a code point that is invalid in CCSID 1047 but is ASCII 'f'.
// This is the minimal reproducer for the leading-run heuristic defect.
static const unsigned char kEbcdicWithBadByte[] = {
    0x85, 0x83, 0x88, 0x96, 0x40, 0x81, 0xa2, 0x84, 0x66, 0x15};

// An untagged EBCDIC script containing two X'66' bytes (28 bytes total,
// 26 valid CP1047): "file aaa\x66aaa\nls -l aaa\x66aaa\n".
static const unsigned char kDirtyScript[] = {
    0x86, 0x89, 0x93, 0x85, 0x40, 0x81, 0x81, 0x81, 0x66, 0x81, 0x81,
    0x81, 0x15, 0x93, 0xa2, 0x40, 0x60, 0x93, 0x40, 0x40, 0x81, 0x81,
    0x81, 0x66, 0x81, 0x81, 0x81, 0x15};

// Exactly 80% valid CP1047 (8 valid bytes + 2 bad bytes): at the
// conversion threshold, must still be treated as EBCDIC.
static const unsigned char kEbcdic80Percent[] = {
    0x85, 0x83, 0x88, 0x96, 0x40, 0x81, 0xa2, 0x84, 0x66, 0x66};

// 70% valid CP1047 (7 valid bytes + 3 bad bytes): below the threshold,
// must not be treated as EBCDIC.
static const unsigned char kEbcdic70Percent[] = {
    0x85, 0x83, 0x88, 0x96, 0x40, 0x81, 0xa2, 0x66, 0x66, 0x66};

// Plain ASCII text: must never be converted.
static const char kAscii[] = "echo asdf\n";

// UTF-8 text (non-ASCII): must never be converted.
static const char kUtf8[] = "echo caf\xc3\xa9\n";

// The converted form of kCleanEbcdic / kEbcdicWithBadByte prefix.
static const char kEchoAsd[] = "echo asd";

class UntaggedReadTest : public ::testing::Test {
protected:
  void SetUp() override {
    setenv("__UNTAGGED_READ_MODE", "AUTO", 1);
    __update_envar_settings("__UNTAGGED_READ_MODE");
    __set_ccsid_guess_buf_size(4096);
    temp_path = tmpnam(NULL);
    ASSERT_TRUE(temp_path != NULL);
  }

  void TearDown() override {
    setenv("__UNTAGGED_READ_MODE", "AUTO", 1);
    __update_envar_settings("__UNTAGGED_READ_MODE");
    __set_ccsid_guess_buf_size(4096);
    remove(temp_path);
  }

  // Write buf to an untagged temp file, byte-for-byte.
  int WriteUntagged(const void *buf, size_t len) {
    remove(temp_path);
    int fd = open(temp_path, O_CREAT | O_WRONLY, 0777);
    if (fd < 0)
      return -1;
    // New files are tagged ASCII (819) by the open hook; make untagged.
    __setfdccsid(fd, 0);
    const char *p = (const char *)buf;
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
    return 0;
  }

  // Open the temp file read-only through the hooked open(). The hook runs
  // the untagged-file heuristic; the resulting fd ccsid reports the
  // decision: 0x10000 + 1047 when EBCDIC conversion was enabled, 0 when
  // the file is read without conversion.
  int OpenUntagged(long *fd_ccsid) {
    int fd = open(temp_path, O_RDONLY);
    if (fd < 0)
      return -1;
    if (fd_ccsid)
      *fd_ccsid = __getfdccsid(fd);
    return fd;
  }

  // Convenience: write + open, returns fd with the heuristic applied.
  int SetupFile(const void *buf, size_t len, long *fd_ccsid) {
    if (WriteUntagged(buf, len) != 0)
      return -1;
    return OpenUntagged(fd_ccsid);
  }

  const char *temp_path;
};

// --- Decision matrix -------------------------------------------------------

TEST_F(UntaggedReadTest, CleanEbcdicConverts) {
  long ccsid = -1;
  int fd = SetupFile(kCleanEbcdic, sizeof(kCleanEbcdic), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, EbcdicWithSingleBadByteConverts) {
  // X'66' (invalid 1047, ASCII 'f') must not flip the guess to ASCII.
  long ccsid = -1;
  int fd = SetupFile(kEbcdicWithBadByte, sizeof(kEbcdicWithBadByte), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, DirtyEbcdicScriptConverts) {
  long ccsid = -1;
  int fd = SetupFile(kDirtyScript, sizeof(kDirtyScript), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, EbcdicAt80PercentThresholdConverts) {
  long ccsid = -1;
  int fd = SetupFile(kEbcdic80Percent, sizeof(kEbcdic80Percent), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, EbcdicBelowThresholdDoesNotConvert) {
  long ccsid = -1;
  int fd = SetupFile(kEbcdic70Percent, sizeof(kEbcdic70Percent), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, AsciiDoesNotConvert) {
  long ccsid = -1;
  int fd = SetupFile(kAscii, strlen(kAscii), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, Utf8DoesNotConvert) {
  long ccsid = -1;
  int fd = SetupFile(kUtf8, strlen(kUtf8), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, Utf8TwoByteSequenceDoesNotConvert) {
  // Valid 2-byte UTF-8 (C4 81, U+0101) must not be rejected and
  // converted as EBCDIC.
  static const char kUtf8TwoByte[] = "abc\xc4\x81" "def\n";
  long ccsid = -1;
  int fd = SetupFile(kUtf8TwoByte, strlen(kUtf8TwoByte), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, Utf8SplitByScanWindowDoesNotConvert) {
  // Regression test: a UTF-8 file whose scan window ends in the middle
  // of a multi-byte character must not be misdetected as EBCDIC.
  // "@@@" + U+3042 (E3 81 82): every sampled byte is also a valid
  // CP1047 code point, so a broken split-sequence handling would score
  // 100% EBCDIC and be caught by this test.
  static const char kUtf8ThreeByte[] = "@@@\xe3\x81\x82";
  long ccsid = -1;
  int fd = SetupFile(kUtf8ThreeByte, strlen(kUtf8ThreeByte), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);

  // Scan window of 4 ends right after the lead byte E3, but the file
  // (6 bytes) extends beyond the sample: the lookahead must complete
  // the sequence, so no conversion must happen.
  __set_ccsid_guess_buf_size(4);
  fd = SetupFile(kUtf8ThreeByte, strlen(kUtf8ThreeByte), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
  __set_ccsid_guess_buf_size(4096);
}

TEST_F(UntaggedReadTest, ConsecutiveMultibyteSplitByWindowDoesNotConvert) {
  // Regression test: a file of repeated 3-byte UTF-8 characters
  // (U+3042 = E3 81 82) whose default scan window ends one byte into
  // the last character. The lookahead must read exactly the 2
  // continuation bytes the pending sequence needs; reading a fixed
  // 3 bytes would complete the character but land mid-way into the
  // next one, misclassifying this 100%-CP1047-valid sample as EBCDIC.
  // 1366 repeats = 4098 bytes; the 4096-byte window ends after a
  // lone E3.
  enum { N = 1366 };
  unsigned char *buf = (unsigned char *)malloc(N * 3);
  ASSERT_TRUE(buf != NULL);
  for (int i = 0; i < N; i++) {
    buf[i * 3] = 0xe3;
    buf[i * 3 + 1] = 0x81;
    buf[i * 3 + 2] = 0x82;
  }
  long ccsid = -1;
  int fd = SetupFile(buf, N * 3, &ccsid);
  free(buf);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, SurrogateSequenceRejectedConverts) {
  // Regression test: utf8scan must reject surrogate encodings
  // (U+D800-U+DFFF). 4093 EBCDIC spaces + ED A0 81 (U+D801, invalid)
  // previously passed the UTF-8 scan, leaving this 99%-CP1047-valid
  // sample unconverted.
  unsigned char buf[4096];
  memset(buf, 0x40, 4093);
  buf[4093] = 0xed;
  buf[4094] = 0xa0;
  buf[4095] = 0x81;
  long ccsid = -1;
  int fd = SetupFile(buf, sizeof(buf), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, ShortLookaheadEofEbcdicStillConverts) {
  // Regression test: when the lookahead reaches EOF before reading
  // all pending continuation bytes, the sample must NOT be accepted
  // as ASCII. 4095 EBCDIC spaces + E3 (3-byte lead) + 15 (newline):
  // the window ends on E3, but only one byte remains in the file, so
  // the pending sequence cannot complete -- this 100%-CP1047-valid
  // file must convert.
  unsigned char buf[4097];
  memset(buf, 0x40, 4095);
  buf[4095] = 0xe3;
  buf[4096] = 0x15;
  long ccsid = -1;
  int fd = SetupFile(buf, sizeof(buf), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, NulInLookaheadDoesNotConvert) {
  // Regression test: a NUL byte arriving in the lookahead (past the
  // scan window) means binary data and must disqualify conversion,
  // even though the scanned sample (40 40 40 C8) is 100% valid
  // CP1047. With a 4-byte window: 40 40 40 C8 00.
  static const unsigned char kNulAfterDanglingLead[] = {
      0x40, 0x40, 0x40, 0xc8, 0x00};
  __set_ccsid_guess_buf_size(4);
  long ccsid = -1;
  int fd =
      SetupFile(kNulAfterDanglingLead, sizeof(kNulAfterDanglingLead), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
  __set_ccsid_guess_buf_size(4096);
}

TEST_F(UntaggedReadTest, ExactWindowEofEbcdicStillConverts) {
  // Regression test: a file exactly the size of the scan window that
  // ends in a byte which is also a valid UTF-8 lead (X'C8' = EBCDIC 'H')
  // is NOT truncated -- the whole file was scanned -- so the dangling
  // tail must be treated as invalid UTF-8 and the file classified as
  // EBCDIC.
  static const unsigned char kSpacesThenH[] = {0x40, 0x40, 0x40, 0xc8};
  __set_ccsid_guess_buf_size(4);  // window == file size exactly
  long ccsid = -1;
  int fd = SetupFile(kSpacesThenH, sizeof(kSpacesThenH), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
  __set_ccsid_guess_buf_size(4096);

  // With the default window the sample is a strict prefix of the file
  // (file continues beyond the scanned bytes)... still whole file, so
  // also no truncation: same EBCDIC result.
  fd = SetupFile(kSpacesThenH, sizeof(kSpacesThenH), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, DanglingLeadWithNonContinuationNextConverts) {
  // Regression test: a dangling lead byte at the scan-window boundary
  // does not prove a UTF-8 character follows. 4095 EBCDIC spaces +
  // X'C8' ('H') + X'15' (newline): the window ends on X'C8', but the
  // lookahead byte X'15' cannot complete the sequence, so the file
  // must be classified as EBCDIC (the sample is 100% valid CP1047).
  unsigned char buf[4097];
  memset(buf, 0x40, 4095);
  buf[4095] = 0xc8;
  buf[4096] = 0x15;
  long ccsid = -1;
  int fd = SetupFile(buf, sizeof(buf), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

TEST_F(UntaggedReadTest, InvalidLeadByteSmallWindowConverts) {
  // Regression test: F5-F7 are not valid UTF-8 lead bytes (only F0-F4
  // are). A file of EBCDIC X'F5' bytes with a 1-byte scan window must
  // not be accepted as truncated UTF-8.
  static const unsigned char kF5F5[] = {0xf5, 0xf5};
  __set_ccsid_guess_buf_size(1);
  long ccsid = -1;
  int fd = SetupFile(kF5F5, sizeof(kF5F5), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
  __set_ccsid_guess_buf_size(4096);
}

TEST_F(UntaggedReadTest, BinaryDoesNotConvert) {
  unsigned char binary[256];
  for (int i = 0; i < 256; i++)
    binary[i] = (unsigned char)i;
  long ccsid = -1;
  int fd = SetupFile(binary, sizeof(binary), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, NulPaddedBinaryDoesNotConvert) {
  // Regression test (binary corner case): a fixed-record or padded
  // binary whose bytes are mostly CP1047-valid (X'40' padding scores
  // 97%) must not be converted once NUL bytes are present -- NUL never
  // appears in CP1047 text.
  unsigned char buf[310];
  memset(buf, 0x40, 300);
  memset(buf + 300, 0x00, 10);  // NUL-filled tail
  long ccsid = -1;
  int fd = SetupFile(buf, sizeof(buf), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, FixedRecordBinaryFieldsDoesNotConvert) {
  // Regression test (binary corner case): fixed-record structured data
  // with X'40' text-ish fields but binary (NUL-containing) field values
  // scores ~95% valid CP1047 -- must not be converted.
  unsigned char rec[81];
  rec[0] = 0x12; rec[1] = 0x34; rec[2] = 0x00; rec[3] = 0x00;
  memset(rec + 4, 0x40, 76);
  rec[80] = 0x15;
  unsigned char buf[40 * 81];
  for (int i = 0; i < 40; i++)
    memcpy(buf + i * 81, rec, 81);
  long ccsid = -1;
  int fd = SetupFile(buf, sizeof(buf), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, AsciiStringTableWithNulsDoesNotConvert) {
  // Binary corner case: an archive/object file whose first 4KB is an
  // ASCII string table (NUL-terminated strings). Not valid UTF-8
  // (NULs), and ASCII text scores low on CP1047 validity (~28%):
  // must pass through unconverted.
  static const char pattern[] = "name\0version\0description\0";
  unsigned char buf[4096];
  for (size_t off = 0; off + sizeof(pattern) - 1 <= sizeof(buf);
       off += sizeof(pattern) - 1)
    memcpy(buf + off, pattern, sizeof(pattern) - 1);
  long ccsid = -1;
  int fd = SetupFile(buf, sizeof(buf), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);
}

TEST_F(UntaggedReadTest, EmptyFileConverts) {
  // Historical behavior: empty untagged files are treated as EBCDIC.
  long ccsid = -1;
  int fd = SetupFile("", 0, &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

// --- End-to-end read-back --------------------------------------------------

TEST_F(UntaggedReadTest, ReadBackCleanEbcdic) {
  long ccsid = -1;
  int fd = SetupFile(kCleanEbcdic, sizeof(kCleanEbcdic), &ccsid);
  ASSERT_GE(fd, 0);
  char buf[64];
  memset(buf, 0, sizeof(buf));
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  ASSERT_EQ(n, (ssize_t)sizeof(kCleanEbcdic));
  EXPECT_STREQ(buf, "echo asdf\n");
}

TEST_F(UntaggedReadTest, ReadBackEbcdicWithBadByte) {
  // End-to-end: the converted content must be "echo asd" +
  // <converted X'66'> + "f\n", i.e. the script must be read as
  // EBCDIC text, not raw EBCDIC bytes. The replacement byte for the
  // unassigned X'66' code point is converter-dependent (it may be
  // substituted or dropped), so only assert what is stable: the converted
  // prefix, the trailing newline, and that this is NOT the raw EBCDIC data.
  long ccsid = -1;
  int fd = SetupFile(kEbcdicWithBadByte, sizeof(kEbcdicWithBadByte), &ccsid);
  ASSERT_GE(fd, 0);
  char buf[64];
  memset(buf, 0, sizeof(buf));
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  ASSERT_GE(n, 9);  // "echo asd" (8 bytes) + newline at minimum; fatal before indexing
  EXPECT_EQ(strncmp(buf, kEchoAsd, strlen(kEchoAsd)), 0);
  EXPECT_EQ(buf[n - 1], '\n');
  EXPECT_NE(memcmp(buf, kEbcdicWithBadByte, sizeof(kEbcdicWithBadByte)), 0);
}

TEST_F(UntaggedReadTest, ReadBackAsciiUnchanged) {
  long ccsid = -1;
  int fd = SetupFile(kAscii, strlen(kAscii), &ccsid);
  ASSERT_GE(fd, 0);
  char buf[64];
  memset(buf, 0, sizeof(buf));
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  ASSERT_EQ(n, (ssize_t)strlen(kAscii));
  EXPECT_STREQ(buf, kAscii);
}

// --- __UNTAGGED_READ_MODE controls -----------------------------------------

TEST_F(UntaggedReadTest, StrictDisablesConversion) {
  setenv("__UNTAGGED_READ_MODE", "STRICT", 1);
  __update_envar_settings("__UNTAGGED_READ_MODE");
  long ccsid = -1;
  int fd = SetupFile(kCleanEbcdic, sizeof(kCleanEbcdic), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);  // no conversion, even for clean EBCDIC
  // Read-back must be the raw EBCDIC bytes.
  char buf[64];
  memset(buf, 0, sizeof(buf));
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  ASSERT_EQ(n, (ssize_t)sizeof(kCleanEbcdic));
  EXPECT_EQ(memcmp(buf, kCleanEbcdic, sizeof(kCleanEbcdic)), 0);
}

TEST_F(UntaggedReadTest, ForcedModeConvertsUntagged) {
  // The forced-conversion mode (V6 on standard builds, ASCII on
  // ZOSLIB_GENERIC builds such as the zopen toolchain) must convert all
  // untagged reads 1047 -> 819 regardless of content. Detect which
  // spelling this build accepts, then verify it forces conversion.
  const char *spellings[] = {"ASCII", "V6"};
  const char *accepted = NULL;
  long ccsid = -1;
  for (size_t i = 0; i < sizeof(spellings) / sizeof(spellings[0]); i++) {
    setenv("__UNTAGGED_READ_MODE", spellings[i], 1);
    __update_envar_settings("__UNTAGGED_READ_MODE");
    int fd = SetupFile(kAscii, strlen(kAscii), &ccsid);
    if (fd >= 0) {
      close(fd);
      if (ccsid == 0x10000 + 1047) {
        accepted = spellings[i];
        break;
      }
    }
  }
  ASSERT_TRUE(accepted != NULL)
      << "neither ASCII nor V6 enabled forced untagged conversion";
  // Under forced mode, even clean ASCII content gets 1047->819 conversion
  // enabled on the fd (this mirrors Rocket bash / Node.js V6 semantics).
  SUCCEED();
}

TEST_F(UntaggedReadTest, UnknownValueFallsBackToAuto) {
  setenv("__UNTAGGED_READ_MODE", "BOGUS_VALUE", 1);
  __update_envar_settings("__UNTAGGED_READ_MODE");
  long ccsid = -1;
  int fd = SetupFile(kCleanEbcdic, sizeof(kCleanEbcdic), &ccsid);
  ASSERT_GE(fd, 0);
  // AUTO behavior: clean EBCDIC still converts.
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
}

// --- __CCSIDGUESSBUFSIZE ----------------------------------------------------

TEST_F(UntaggedReadTest, GuessBufSizeIsHonored) {
  // 4 valid EBCDIC bytes followed by 6 bad bytes. With the default 4KB
  // scan window only 40% of the file is valid CP1047: no conversion.
  // With a 4-byte scan window the scanned prefix is 100% valid CP1047:
  // conversion. This verifies the scan length is actually configurable.
  static const unsigned char kPrefixThenBad[] = {
      0x85, 0x83, 0x88, 0x96, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66};

  long ccsid = -1;
  int fd = SetupFile(kPrefixThenBad, sizeof(kPrefixThenBad), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0);
  close(fd);

  __set_ccsid_guess_buf_size(4);
  fd = SetupFile(kPrefixThenBad, sizeof(kPrefixThenBad), &ccsid);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(ccsid, 0x10000 + 1047);
  close(fd);
  __set_ccsid_guess_buf_size(4096);  // restore (also done in TearDown)
}

}  // namespace
