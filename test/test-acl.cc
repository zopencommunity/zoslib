// Tests for the POSIX 1003.1e style ACL API, see include/sys/acl.h.

// Enable CLIB overrides
#define ZOSLIB_OVERRIDE_CLIB 1

#include "zos.h"
#include "gtest/gtest.h"

#include <sys/acl.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Number of extended ACL entries in the working storage of a handle.
static int aclEntryCount(acl_t acl) {
  return acl->lacl_data.aclb_entry.acl_entnum;
}

// Extended ACL entry "i" of a handle.
static struct ACL_entry *aclEntry(acl_t acl, int i) {
  return (struct ACL_entry *)((char *)&acl->lacl_data + sizeof(struct ACL_buf)) + i;
}

// An ACL entry qualifier: a user or group id.
struct Qualifier {
  int type; // ACL_USER or ACL_GROUP
  long id;  // user or group id
};

// Collects up to "count" distinct non-zero qualifiers known to the
// security product.  The current user and group are always available;
// further ids come from the account and group databases.  The tests must
// not skip: the CI check counts a skipped test as a failure.
static std::vector<Qualifier> collectQualifiers(int count) {
  std::vector<Qualifier> quals;
  auto add = [&quals](int type, long id) {
    if (id == 0)
      return;
    for (const auto &q : quals)
      if (q.type == type && q.id == id)
        return;
    Qualifier q = {type, id};
    quals.push_back(q);
  };

  add(ACL_USER, getuid());
  add(ACL_GROUP, getgid());

  setpwent();
  struct passwd *pw;
  while ((pw = getpwent()) != NULL && (int)quals.size() < count)
    add(ACL_USER, pw->pw_uid);
  endpwent();

  setgrent();
  struct group *gr;
  while ((gr = getgrent()) != NULL && (int)quals.size() < count)
    add(ACL_GROUP, gr->gr_gid);
  endgrent();

  return quals;
}

// Internal, see src/zos-acl.c: initial working storage size, shrunk by
// the tests to force the E2BIG growth path.
extern "C" void __acl_set_initial_count(int count);

// The setfacl spec of a qualifier, e.g. "u:123:rwx".
static std::string specOf(const Qualifier &q) {
  char spec[64];
  snprintf(spec, sizeof(spec), "%c:%ld:rwx", q.type == ACL_USER ? 'u' : 'g', q.id);
  return spec;
}

class AclTest : public ::testing::Test {
protected:
  void SetUp() override {
    strcpy(src_path_, "/tmp/zoslib_acl_src_XXXXXX");
    strcpy(dst_path_, "/tmp/zoslib_acl_dst_XXXXXX");
    src_fd_ = mkstemp(src_path_);
    dst_fd_ = mkstemp(dst_path_);
    ASSERT_GE(src_fd_, 0);
    ASSERT_GE(dst_fd_, 0);
  }

  void TearDown() override {
    if (src_fd_ >= 0)
      close(src_fd_);
    if (dst_fd_ >= 0)
      close(dst_fd_);
    remove(src_path_);
    remove(dst_path_);
  }

  // Installs one extended ACL entry per qualifier on the source file,
  // with read/write/execute permissions, the same way a user would.
  // Note: system() takes the command in ASCII here, the Language
  // Environment ASCII entry points convert it.
  void installEntries(const std::vector<Qualifier> &quals, const char *path) {
    for (const auto &q : quals) {
      char cmd[1024];
      snprintf(cmd, sizeof(cmd), "setfacl -m %s %s", specOf(q).c_str(), path);
      ASSERT_EQ(system(cmd), 0) << "setfacl failed: " << cmd;
    }
  }

  char src_path_[64];
  char dst_path_[64];
  int src_fd_ = -1;
  int dst_fd_ = -1;
};

// An ACL retrieved from one file can be applied to another file, with
// all entries and permissions intact.  This is the "preserve the ACL
// when replacing a file" use case.
TEST_F(AclTest, RoundTrip) {
  std::vector<Qualifier> quals;
  Qualifier q = {ACL_USER, getuid()};
  quals.push_back(q);
  installEntries(quals, src_path_);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl), 1);
  EXPECT_EQ(aclEntry(acl, 0)->acle_type, ACL_USER);
  EXPECT_EQ(aclEntry(acl, 0)->acle_id, getuid());
  EXPECT_EQ(aclEntry(acl, 0)->acle_value.aclp_read, 1);
  EXPECT_EQ(aclEntry(acl, 0)->acle_value.aclp_write, 1);
  EXPECT_EQ(aclEntry(acl, 0)->acle_value.aclp_execute, 1);

  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), 0);
  EXPECT_EQ(acl_free(acl), 0);

  // Read the destination file back and compare.
  acl_t acl2 = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl2, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl2), 1);
  EXPECT_EQ(aclEntry(acl2, 0)->acle_type, ACL_USER);
  EXPECT_EQ(aclEntry(acl2, 0)->acle_id, getuid());
  EXPECT_EQ(aclEntry(acl2, 0)->acle_value.aclp_read, 1);
  EXPECT_EQ(aclEntry(acl2, 0)->acle_value.aclp_write, 1);
  EXPECT_EQ(aclEntry(acl2, 0)->acle_value.aclp_execute, 1);
  EXPECT_EQ(acl_free(acl2), 0);
}

// A file with more ACL entries than the initial working storage size
// (16) must still be retrieved; the implementation retries with a larger
// working storage.  Machines with few accounts may not reach 16 entries;
// the test then checks the round trip with the entries available.
TEST_F(AclTest, ManyEntries) {
  std::vector<Qualifier> quals = collectQualifiers(20);
  ASSERT_GE((int)quals.size(), 2);
  installEntries(quals, src_path_);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl), (int)quals.size());

  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), 0);
  EXPECT_EQ(acl_free(acl), 0);

  acl_t acl2 = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl2, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl2), (int)quals.size());
  // The order of the entries is not guaranteed, compare them as sets.
  std::vector<std::string> got, want;
  for (int i = 0; i < aclEntryCount(acl2); i++) {
    char entry[64];
    snprintf(entry, sizeof(entry), "%d:%ld", aclEntry(acl2, i)->acle_type,
             (long)aclEntry(acl2, i)->acle_id);
    got.push_back(entry);
    EXPECT_EQ(aclEntry(acl2, i)->acle_value.aclp_read, 1);
    EXPECT_EQ(aclEntry(acl2, i)->acle_value.aclp_write, 1);
    EXPECT_EQ(aclEntry(acl2, i)->acle_value.aclp_execute, 1);
  }
  for (const auto &q : quals) {
    char entry[64];
    snprintf(entry, sizeof(entry), "%d:%ld", q.type, q.id);
    want.push_back(entry);
  }
  std::sort(got.begin(), got.end());
  std::sort(want.begin(), want.end());
  EXPECT_EQ(got, want);
  EXPECT_EQ(acl_free(acl2), 0);
}

// Applying an ACL replaces the extended ACL of the destination file:
// entries that only the destination file had must not survive.
TEST_F(AclTest, ReplacesExistingAcl) {
  std::vector<Qualifier> quals = collectQualifiers(2);
  ASSERT_GE((int)quals.size(), 2);

  // Give the destination file an entry the source file does not have.
  std::vector<Qualifier> dst_quals;
  dst_quals.push_back(quals[1]);
  installEntries(dst_quals, dst_path_);

  // Apply the source file's ACL (entry for quals[0]) to the destination.
  std::vector<Qualifier> src_quals;
  src_quals.push_back(quals[0]);
  installEntries(src_quals, src_path_);
  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), 0);
  EXPECT_EQ(acl_free(acl), 0);

  // The destination must now have exactly the source's entry.
  acl_t acl2 = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl2, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl2), 1);
  EXPECT_EQ(aclEntry(acl2, 0)->acle_id, (int)quals[0].id);
  EXPECT_EQ(aclEntry(acl2, 0)->acle_type, quals[0].type);
  EXPECT_EQ(acl_free(acl2), 0);
}

// The working storage growth on E2BIG is forced by shrinking the
// initial size, independent of the machine's account databases: the
// first attempt can never hold the three base entries plus the extended
// entries.
TEST_F(AclTest, E2BigRetry) {
  std::vector<Qualifier> quals = collectQualifiers(5);
  ASSERT_GE((int)quals.size(), 1);
  installEntries(quals, src_path_);

  __acl_set_initial_count(2);
  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  __acl_set_initial_count(16);
  ASSERT_NE(acl, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl), (int)quals.size());

  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), 0);
  EXPECT_EQ(acl_free(acl), 0);

  acl_t acl2 = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl2, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(acl2), (int)quals.size());
  EXPECT_EQ(acl_free(acl2), 0);
}

// Restricted permissions survive the round trip, including replacing a
// broader entry for the same qualifier with a narrower one.
TEST_F(AclTest, RestrictedPermissions) {
  const char *specs[] = {"rwx", "r--", "---", "-w-"};
  for (const char *spec : specs) {
    char cmd[1024];
    // install rwx first, then the restricted spec, so that the last
    // setfacl downgrades the entry
    snprintf(cmd, sizeof(cmd), "setfacl -m u:%ld:rwx %s", (long)getuid(),
             src_path_);
    ASSERT_EQ(system(cmd), 0) << cmd;
    snprintf(cmd, sizeof(cmd), "setfacl -m u:%ld:%s %s", (long)getuid(), spec,
             src_path_);
    ASSERT_EQ(system(cmd), 0) << cmd;

    acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
    ASSERT_NE(acl, (acl_t)NULL);
    ASSERT_EQ(aclEntryCount(acl), 1);
    struct ACL_entry *e = aclEntry(acl, 0);
    EXPECT_EQ(e->acle_value.aclp_read, spec[0] == 'r' ? 1 : 0) << spec;
    EXPECT_EQ(e->acle_value.aclp_write, spec[1] == 'w' ? 1 : 0) << spec;
    EXPECT_EQ(e->acle_value.aclp_execute, spec[2] == 'x' ? 1 : 0) << spec;

    EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), 0);
    EXPECT_EQ(acl_free(acl), 0);

    acl_t acl2 = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
    ASSERT_NE(acl2, (acl_t)NULL);
    ASSERT_EQ(aclEntryCount(acl2), 1);
    e = aclEntry(acl2, 0);
    EXPECT_EQ(e->acle_value.aclp_read, spec[0] == 'r' ? 1 : 0) << spec;
    EXPECT_EQ(e->acle_value.aclp_write, spec[1] == 'w' ? 1 : 0) << spec;
    EXPECT_EQ(e->acle_value.aclp_execute, spec[2] == 'x' ? 1 : 0) << spec;
    EXPECT_EQ(acl_free(acl2), 0);
  }
}

// A file without extended ACL entries yields a handle without entries.
TEST_F(AclTest, NoExtendedAcl) {
  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(acl), 0);
  EXPECT_EQ(acl_free(acl), 0);
}

// Applying an ACL without entries removes the extended ACL of the
// destination file: access restrictions are preserved, not just grants.
TEST_F(AclTest, EmptySourceClearsDestination) {
  std::vector<Qualifier> quals = collectQualifiers(1);
  ASSERT_GE((int)quals.size(), 1);

  // Give the destination file an entry.
  installEntries(quals, dst_path_);
  acl_t check = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(check, (acl_t)NULL);
  ASSERT_GT(aclEntryCount(check), 0);
  EXPECT_EQ(acl_free(check), 0);

  // The source file has no extended ACL entries.
  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(empty), 0);

  // Applying it must remove the destination's entry.
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, empty), 0);
  EXPECT_EQ(acl_free(empty), 0);

  check = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(check, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(check), 0);
  EXPECT_EQ(acl_free(check), 0);
}

// Applying an ACL without entries removes the extended ACL of the
// destination file and restores the base permissions of the source.
TEST_F(AclTest, EmptySourceRestoresBasePermissions) {
  ASSERT_EQ(chmod(src_path_, 0600), 0);
  ASSERT_EQ(chmod(dst_path_, 0644), 0);

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(empty), 0);

  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, empty), 0);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(dst_path_, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0600);
}

// The setuid, setgid and sticky bits are not part of an access ACL and
// survive applying an empty ACL.  z/OS drops setgid on regular files, so
// this uses the sticky bit on a directory.
TEST_F(AclTest, EmptySourceKeepsSpecialBits) {
  char src_dir[] = "/tmp/zoslib_acl_srcd_XXXXXX";
  char dst_dir[] = "/tmp/zoslib_acl_dstd_XXXXXX";
  ASSERT_NE(mkdtemp(src_dir), (char *)NULL);
  ASSERT_NE(mkdtemp(dst_dir), (char *)NULL);
  ASSERT_EQ(chmod(src_dir, 0700), 0);
  ASSERT_EQ(chmod(dst_dir, 0777 | S_ISVTX), 0);

  acl_t empty = acl_get_file(src_dir, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(empty), 0);
  EXPECT_EQ(acl_set_file(dst_dir, ACL_TYPE_ACCESS, empty), 0);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(dst_dir, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0700);
  EXPECT_EQ(st.st_mode & S_ISVTX, (mode_t)S_ISVTX);

  rmdir(src_dir);
  rmdir(dst_dir);
}

// When the base permissions cannot be applied, the destination is left
// completely unchanged.  A dangling symlink makes chmod() fail with
// ENOENT without touching any live system object.
TEST_F(AclTest, SetFailsWhenModeCannotBeApplied) {
  char target[64], link[64];
  snprintf(target, sizeof(target), "%s.no_target", src_path_);
  snprintf(link, sizeof(link), "%s.dangling", src_path_);
  ASSERT_EQ(symlink(target, link), 0);

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  errno = 0;
  EXPECT_EQ(acl_set_file(link, ACL_TYPE_ACCESS, empty), -1);
  EXPECT_EQ(errno, ENOENT);
  EXPECT_EQ(acl_free(empty), 0);

  // The symlink itself is untouched.
  struct stat st;
  ASSERT_EQ(lstat(link, &st), 0);
  EXPECT_TRUE(S_ISLNK(st.st_mode));
  remove(link);
}

// The update runs through a file descriptor, so a restrictive new mode
// cannot make the removal fail to resolve the destination path (as in
// "dir/." with an incoming mode of 0000).
TEST_F(AclTest, RestrictiveModeOnDotPath) {
  char dir[64], dot[70];
  snprintf(dir, sizeof(dir), "/tmp/zoslib_acl_dir_XXXXXX");
  ASSERT_NE(mkdtemp(dir), (char *)NULL);
  snprintf(dot, sizeof(dot), "%s/.", dir);
  ASSERT_EQ(chmod(dir, 0755), 0);
  ASSERT_EQ(chmod(src_path_, 0000), 0);

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(dot, ACL_TYPE_ACCESS, empty), 0);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(dir, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0000);

  // Restore for the cleanup; plain chmod on the path still works.
  ASSERT_EQ(chmod(dir, 0755), 0);
  rmdir(dir);
}

// The base permissions are also restored through the non-empty path:
// a broader destination mode is narrowed to the source's mode.
TEST_F(AclTest, NonEmptySourceRestoresBasePermissions) {
  ASSERT_EQ(chmod(src_path_, 0600), 0);
  ASSERT_EQ(chmod(dst_path_, 0666), 0);

  std::vector<Qualifier> quals;
  Qualifier q = {ACL_USER, getuid()};
  quals.push_back(q);
  installEntries(quals, src_path_);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), 0);
  EXPECT_EQ(acl_free(acl), 0);

  struct stat st;
  ASSERT_EQ(stat(dst_path_, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0600);

  acl_t acl2 = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl2, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(acl2), 1);
  EXPECT_EQ(acl_free(acl2), 0);
}

// Opening a FIFO for reading must not block: the update of a FIFO
// destination has to complete without a writer ever connecting.
TEST_F(AclTest, EmptyAclOnFifo) {
  char fifo[64];
  snprintf(fifo, sizeof(fifo), "%s.fifo", src_path_);
  ASSERT_EQ(mkfifo(fifo, 0666), 0);
  ASSERT_EQ(chmod(fifo, 0666), 0);
  ASSERT_EQ(chmod(src_path_, 0600), 0);

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(fifo, ACL_TYPE_ACCESS, empty), 0);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(fifo, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0600);
  remove(fifo);
}

// An initially unreadable destination (execute-only directory) cannot
// be pinned with a descriptor; the path based fallback must still
// complete the update.
TEST_F(AclTest, RestrictiveModeOnUnreadableDir) {
  char dir[64], dot[70];
  snprintf(dir, sizeof(dir), "/tmp/zoslib_acl_xdir_XXXXXX");
  ASSERT_NE(mkdtemp(dir), (char *)NULL);
  snprintf(dot, sizeof(dot), "%s/.", dir);
  ASSERT_EQ(chmod(dir, 0111), 0);
  ASSERT_EQ(chmod(src_path_, 0000), 0);

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(dot, ACL_TYPE_ACCESS, empty), 0);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(dir, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0000);

  ASSERT_EQ(chmod(dir, 0755), 0);
  rmdir(dir);
}

// An unreadable destination that has an extended ACL is refused rather
// than updated unsafely: both its mode and its ACL stay unchanged.
TEST_F(AclTest, UnreadableDirWithAclIsRefused) {
  char dir[64], dot[70];
  snprintf(dir, sizeof(dir), "/tmp/zoslib_acl_rdir_XXXXXX");
  ASSERT_NE(mkdtemp(dir), (char *)NULL);
  snprintf(dot, sizeof(dot), "%s/.", dir);
  ASSERT_EQ(chmod(dir, 0755), 0);

  std::vector<Qualifier> quals;
  Qualifier q = {ACL_USER, getuid()};
  quals.push_back(q);
  installEntries(quals, dir);
  acl_t before = acl_get_file(dir, ACL_TYPE_ACCESS);
  ASSERT_NE(before, (acl_t)NULL);
  ASSERT_GT(aclEntryCount(before), 0);
  EXPECT_EQ(acl_free(before), 0);

  ASSERT_EQ(chmod(dir, 0111), 0);
  ASSERT_EQ(chmod(src_path_, 0000), 0);
  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(dot, ACL_TYPE_ACCESS, empty), -1);
  EXPECT_EQ(acl_free(empty), 0);

  // Nothing changed.
  struct stat st;
  ASSERT_EQ(stat(dir, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0111);
  acl_t after = acl_get_file(dir, ACL_TYPE_ACCESS);
  ASSERT_NE(after, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(after), 1);
  EXPECT_EQ(acl_free(after), 0);

  ASSERT_EQ(chmod(dir, 0755), 0);
  rmdir(dir);
}

// The special bits survive applying a non-empty ACL as well: the set
// request clears them, so they are restored afterwards.
TEST_F(AclTest, NonEmptySourceKeepsSpecialBits) {
  char dir[64];
  snprintf(dir, sizeof(dir), "/tmp/zoslib_acl_sdir_XXXXXX");
  ASSERT_NE(mkdtemp(dir), (char *)NULL);
  ASSERT_EQ(chmod(dir, 0755), 0);
  std::vector<Qualifier> quals;
  Qualifier q = {ACL_USER, getuid()};
  quals.push_back(q);
  installEntries(quals, dir);
  // Sticky, but without group/other write+search: a directory whose
  // mode or ACL grants those cannot be rolled back safely and is
  // refused (see StickyDirWithUnrestorableAclIsRefused).
  ASSERT_EQ(chmod(dir, 01750), 0);

  std::vector<Qualifier> src_quals;
  src_quals.push_back(q);
  installEntries(src_quals, src_path_);
  ASSERT_EQ(chmod(src_path_, 0750), 0);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  EXPECT_EQ(acl_set_file(dir, ACL_TYPE_ACCESS, acl), 0);
  EXPECT_EQ(acl_free(acl), 0);

  struct stat st;
  ASSERT_EQ(stat(dir, &st), 0);
  EXPECT_EQ(st.st_mode & 0777, (mode_t)0750);
  EXPECT_EQ(st.st_mode & S_ISVTX, (mode_t)S_ISVTX);

  ASSERT_EQ(chmod(dir, 0755), 0);
  rmdir(dir);
}

// A sticky directory whose saved ACL grants write and search to
// non-owners cannot be rolled back safely (restoring the entries needs
// the set request, which clears the sticky bit): the update is refused.
TEST_F(AclTest, StickyDirWithUnrestorableAclIsRefused) {
  char dir[64];
  snprintf(dir, sizeof(dir), "/tmp/zoslib_acl_udir_XXXXXX");
  ASSERT_NE(mkdtemp(dir), (char *)NULL);
  ASSERT_EQ(chmod(dir, 0755), 0);

  // Another user with write and search on the directory.
  std::vector<Qualifier> quals = collectQualifiers(2);
  const Qualifier *other = NULL;
  for (const auto &q : quals)
    if (q.type != ACL_USER || q.id != (long)getuid())
      other = &q;
  ASSERT_NE(other, (const Qualifier *)NULL);
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "setfacl -m %s %s", specOf(*other).c_str(), dir);
  ASSERT_EQ(system(cmd), 0) << cmd;
  ASSERT_EQ(chmod(dir, 01755), 0);

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  errno = 0;
  EXPECT_EQ(acl_set_file(dir, ACL_TYPE_ACCESS, empty), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_EQ(acl_free(empty), 0);

  // Nothing changed: mode and entry.
  struct stat st;
  ASSERT_EQ(stat(dir, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, (mode_t)01755);
  acl_t after = acl_get_file(dir, ACL_TYPE_ACCESS);
  ASSERT_NE(after, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(after), 1);
  EXPECT_EQ(acl_free(after), 0);

  ASSERT_EQ(chmod(dir, 0755), 0);
  rmdir(dir);
}

// A sticky directory whose incoming ACL grants write and search to
// non-owners is refused: between the set request and the restoration
// of the sticky bit those users could unlink other users' files.
TEST_F(AclTest, StickyDirWithWriteSearchIsRefused) {
  char dir[64];
  snprintf(dir, sizeof(dir), "/tmp/zoslib_acl_wdir_XXXXXX");
  ASSERT_NE(mkdtemp(dir), (char *)NULL);
  ASSERT_EQ(chmod(dir, 01775), 0);

  // Another user (or group) with write and search in the incoming ACL.
  std::vector<Qualifier> quals = collectQualifiers(2);
  const Qualifier *other = NULL;
  for (const auto &q : quals)
    if (q.type != ACL_USER || q.id != (long)getuid())
      other = &q;
  ASSERT_NE(other, (const Qualifier *)NULL);
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "setfacl -m %s %s", specOf(*other).c_str(),
           src_path_);
  ASSERT_EQ(system(cmd), 0) << cmd;
  ASSERT_EQ(chmod(src_path_, 0750), 0);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  errno = 0;
  EXPECT_EQ(acl_set_file(dir, ACL_TYPE_ACCESS, acl), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_EQ(acl_free(acl), 0);

  // Nothing changed.
  struct stat st;
  ASSERT_EQ(stat(dir, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, (mode_t)01775);

  ASSERT_EQ(chmod(dir, 0755), 0);
  rmdir(dir);
}

// A setuid destination whose incoming ACL grants write to non-owners
// is refused: while the privilege bits are cleared between the set
// request and their restoration, that user could modify the contents
// and the restoration would make them privileged.
TEST_F(AclTest, SetuidWithWriteGrantIsRefused) {
  ASSERT_EQ(chmod(dst_path_, 04755), 0);

  // Another user with write access in the incoming ACL.
  std::vector<Qualifier> quals = collectQualifiers(2);
  const Qualifier *other = NULL;
  for (const auto &q : quals)
    if (q.type != ACL_USER || q.id != (long)getuid())
      other = &q;
  ASSERT_NE(other, (const Qualifier *)NULL);
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "setfacl -m %s %s", specOf(*other).c_str(),
           src_path_);
  ASSERT_EQ(system(cmd), 0) << cmd;
  ASSERT_EQ(chmod(src_path_, 0750), 0);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  errno = 0;
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_EQ(acl_free(acl), 0);

  // Nothing changed.
  struct stat st;
  ASSERT_EQ(stat(dst_path_, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, (mode_t)04755);
  acl_t after = acl_get_file(dst_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(after, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(after), 0);
  EXPECT_EQ(acl_free(after), 0);
}

// A setuid destination whose own mode grants write to group or other
// is refused even when the incoming ACL is narrow: a non-owner may hold
// a descriptor opened for writing before the update, and the privilege
// restoration would arm modified content.
TEST_F(AclTest, SetuidWithWritableModeIsRefused) {
  ASSERT_EQ(chmod(dst_path_, 04777), 0);
  std::vector<Qualifier> quals;
  Qualifier q = {ACL_USER, getuid()};
  quals.push_back(q);
  installEntries(quals, src_path_);
  ASSERT_EQ(chmod(src_path_, 0750), 0);

  acl_t acl = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(acl, (acl_t)NULL);
  ASSERT_EQ(aclEntryCount(acl), 1);
  errno = 0;
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, acl), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_EQ(acl_free(acl), 0);

  struct stat st;
  ASSERT_EQ(stat(dst_path_, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, (mode_t)04777);
}

// An empty incoming ACL whose base mode grants write to group or other
// is refused on a setuid destination: the final mode would arm
// privilege on writable content.
TEST_F(AclTest, SetuidWithWritableIncomingBaseIsRefused) {
  ASSERT_EQ(chmod(src_path_, 0766), 0);   // group write, no entries
  ASSERT_EQ(chmod(dst_path_, 04750), 0);  // setuid destination

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(empty), 0);
  errno = 0;
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, empty), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(dst_path_, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, (mode_t)04750);
}

// The empty-handle variant: an old mode granting write to group or
// other on a setuid destination is refused as well — a descriptor
// opened for writing before the update keeps write access.
TEST_F(AclTest, SetuidWithWritableModeEmptyHandleIsRefused) {
  ASSERT_EQ(chmod(dst_path_, 04777), 0);
  ASSERT_EQ(chmod(src_path_, 0750), 0);   // narrow empty handle

  acl_t empty = acl_get_file(src_path_, ACL_TYPE_ACCESS);
  ASSERT_NE(empty, (acl_t)NULL);
  EXPECT_EQ(aclEntryCount(empty), 0);
  errno = 0;
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, empty), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_EQ(acl_free(empty), 0);

  struct stat st;
  ASSERT_EQ(stat(dst_path_, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, (mode_t)04777);
}

// A file that does not exist is an error.
TEST_F(AclTest, MissingFile) {
  errno = 0;
  EXPECT_EQ(acl_get_file("/tmp/zoslib_acl_no_such_file", ACL_TYPE_ACCESS),
            (acl_t)NULL);
  EXPECT_EQ(errno, ENOENT);
}

// z/OS has no default ACLs.
TEST_F(AclTest, DefaultTypeRejected) {
  errno = 0;
  EXPECT_EQ(acl_get_file(src_path_, ACL_TYPE_DEFAULT), (acl_t)NULL);
  EXPECT_EQ(errno, EINVAL);
  errno = 0;
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_DEFAULT, NULL), -1);
  EXPECT_EQ(errno, EINVAL);
}

// Applying a NULL handle is a no-op, so callers need no special casing
// for files without an ACL.
TEST_F(AclTest, NullHandleIsNoOp) {
  EXPECT_EQ(acl_set_file(dst_path_, ACL_TYPE_ACCESS, NULL), 0);
}

TEST_F(AclTest, FreeNull) {
  errno = 0;
  EXPECT_EQ(acl_free(NULL), -1);
  EXPECT_EQ(errno, EINVAL);
}

} // namespace
