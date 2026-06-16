# Understanding `_ENCODE_FILE_EXISTING` on z/OS

## Overview

`_ENCODE_FILE_EXISTING` is an environment variable used on z/OS systems to control the automatic tagging of **existing untagged files** when they are written to by applications. This variable works in conjunction with `_ENCODE_FILE_NEW` to provide comprehensive file tagging control.

## Environment Variables

### `_ENCODE_FILE_NEW`
Sets the CCSID (Coded Character Set Identifier) tag for **newly created files**.

### `_ENCODE_FILE_EXISTING`
Sets the CCSID tag for **existing untagged files** when they are opened for writing.

## Syntax

```bash
export _ENCODE_FILE_EXISTING=<value>
```

## Valid Values

- `BINARY` - Treat existing files as binary (no text conversion)
- `<codeset>` - Any valid codeset name (e.g., `ISO8859-1`, `UTF-8`, `IBM-1047`)

## Default Behavior

If `_ENCODE_FILE_EXISTING` is not set, the system uses default file tagging behavior without forcing a specific encoding on existing files.

## How It Works

When an application opens an existing file for writing:
1. If the file is **already tagged**, the existing tag is preserved
2. If the file is **untagged**, it will be automatically tagged with the value specified in `_ENCODE_FILE_EXISTING`

This behavior helps ensure that files have appropriate encoding tags, which is critical for proper text processing on z/OS.

## Verifying File Tags with ls -T

The `ls -T` command displays file tag information on z/OS, showing the CCSID (Coded Character Set Identifier) and text/binary status of files. This is useful for verifying how `_ENCODE_FILE_EXISTING` affects file handling.

---

## Tool-Specific Examples

### 1. Curl

Curl demonstrates how `_ENCODE_FILE_EXISTING` affects downloaded content when writing to existing files.

#### Initial Setup
```bash
export _ENCODE_FILE_NEW=IBM-1047
touch curlout.bin
ls -lT curlout.bin
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun  9 09:56 curlout.bin
```

#### Setting `_ENCODE_FILE_EXISTING`
```bash
export _ENCODE_FILE_EXISTING=ISO8859-1
env | grep _ENCODE_FILE
# Output:
# _ENCODE_FILE_EXISTING=ISO8859-1
# _ENCODE_FILE_NEW=IBM-1047
```

#### First Download (File Already Tagged)
```bash
curl --output curlout.bin https://example.com
ls -lT curlout.bin
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 528 Jun  9 09:57 curlout.bin
```
**Note:** File retains its IBM-1047 tag because it was already tagged.

#### Remove Tag and Download Again
```bash
chtag -r curlout.bin
ls -lT curlout.bin
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 528 Jun  9 09:57 curlout.bin

curl --output curlout.bin https://example.com
ls -lT curlout.bin
# Output: t ISO8859-1   T=on  -rw-rw-r--  1 SABITHA SYSPGMR 528 Jun  9 09:58 curlout.bin
```
**Result:** The untagged file is now tagged with ISO8859-1 (from `_ENCODE_FILE_EXISTING`).

#### Curl with `--tag` Option
Curl also supports explicit tagging via the `--tag` option, which overrides environment variables:

```bash
touch curltag.bin
ls -lT curltag.bin
# Output: t ISO8859-1   T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 10 02:16 curltag.bin

chtag -r curltag.bin
ls -lT curltag.bin
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 10 02:16 curltag.bin

export _ENCODE_FILE_EXISTING=ISO8859-1
curl --tag IBM-1047 -o curltag.bin https://example.com
# Output includes: DEBUG: __chgfdccsid(1047) returns 0, errno=0

ls -lT curltag.bin
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 559 Jun 10 02:18 curltag.bin
```
**Result:** The `--tag` option explicitly sets the tag to IBM-1047, overriding `_ENCODE_FILE_EXISTING`.

---

### 2. Bash (Shell Redirection)

Shell redirection follows the same pattern as other tools.

```bash
touch test.txt
env | grep _ENCODE_FIL
# Output:
# _ENCODE_FILE_EXISTING=IBM-1047
# _ENCODE_FILE_NEW=UTF-8

ls -lT test.txt
# Output: t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 12 09:12 test.txt

chtag -r test.txt
ls -lT test.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 12 09:12 test.txt

echo "test" > test.txt
ls -lT test.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 5 Jun 12 09:13 test.txt
```

**Explanation:**
1. New file created with UTF-8 tag (from `_ENCODE_FILE_NEW`)
2. Tag removed with `chtag -r`
3. Writing to untagged file applies IBM-1047 tag (from `_ENCODE_FILE_EXISTING`)

---

### 3. GPG (GNU Privacy Guard)

GPG output redirection also respects `_ENCODE_FILE_EXISTING`.

```bash
touch gpg_out.txt
ls -lT gpg_out.txt
# Output: t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 15 08:11 gpg_out.txt

env | grep _ENCODE_FILE
# Output:
# _ENCODE_FILE_EXISTING=IBM-1047
# _ENCODE_FILE_NEW=UTF-8

chtag -r gpg_out.txt
ls -lT gpg_out.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 15 08:11 gpg_out.txt

echo "hello zos test" | gpg --armor > gpg_out.txt

ls -lT gpg_out.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 15 08:14 gpg_out.txt
```

**Result:** The untagged file receives the IBM-1047 tag from `_ENCODE_FILE_EXISTING`.

---

### 4. Git

Git on z/OS has special requirements for file tagging and will refuse to work with untagged files.

```bash
echo "hello" > test.txt
ls -lT test.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 10 07:12 test.txt

env | grep _ENCODE_FILE
# Output:
# _ENCODE_FILE_EXISTING=ISO8859-1
# _ENCODE_FILE_NEW=IBM-1047

git diff test.txt
# Output shows diff with encoding issues

chtag -r test.txt
ls -lT test.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 10 07:12 test.txt

git diff test.txt
# Output: fatal: File test.txt is untagged, set the correct file tag (using the chtag command).

git add test.txt
# Output: fatal: File test.txt is untagged, set the correct file tag (using the chtag command)
```

**Important:** Git requires explicit tagging and will not automatically apply `_ENCODE_FILE_EXISTING` tags. You must manually tag files:

```bash
chtag -tc ISO8859-1 test.txt  # For ASCII/UTF-8 content
# or
chtag -tc IBM-1047 test.txt   # For EBCDIC content
```

**Explanation:**
When you created `test.txt` with echo, it was automatically tagged as IBM-1047 because of `_ENCODE_FILE_NEW`. When you removed the tag with `chtag -r`, the file became untagged, and the z/OS git port refuses to work with untagged files as a safety measure.

---

### 5. Coreutils (cat)

Standard Unix utilities like `cat` also respect the tagging environment variables.

```bash
touch test.txt
env | grep _ENCODE_FIL
# Output:
# _ENCODE_FILE_EXISTING=IBM-1047
# _ENCODE_FILE_NEW=UTF-8

ls -lT test.txt
# Output: t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 12 09:12 test.txt

chtag -r test.txt
ls -lT test.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 12 09:12 test.txt

echo "test" > test.txt
cat test.txt
# Output: test

ls -lT test.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 5 Jun 12 09:13 test.txt
```

**Result:** Writing to the untagged file applies the IBM-1047 tag from `_ENCODE_FILE_EXISTING`.

---

### 6. Vim

Vim also respects `_ENCODE_FILE_EXISTING` when reopening an existing untagged file for writing.

```bash
env | grep _ENCODE_FILE
# Output:
# _ENCODE_FILE_EXISTING=IBM-1047
# _ENCODE_FILE_NEW=UTF-8

vim newfile.txt
ls -lT newfile.txt
# Output: t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 15 09:51 newfile.txt

chtag -r newfile.txt
ls -lT newfile.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 15 09:51 newfile.txt

vim newfile.txt
ls -lT newfile.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 15 09:51 newfile.txt

echo "hello" > newfile.txt
chtag -r newfile.txt
cat newfile.txt
# Output: hello

ls -lT newfile.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 15 09:52 newfile.txt

unset _ENCODE_FILE_NEW
unset _ENCODE_FILE_EXISTING
```

**Result:** A newly created file gets the `_ENCODE_FILE_NEW` tag, and when the tag is removed, reopening it in Vim for writing applies `_ENCODE_FILE_EXISTING`.

---

### 7. Tar

The `tar` command also tags existing untagged files when it writes archive output.

```bash
touch file.txt
echo "tarfile" > file.txt
ls -lT file.txt
# Output: t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 8 Jun 15 10:04 file.txt

env | grep _ENCODE_FILE
# Output:
# _ENCODE_FILE_EXISTING=IBM-1047
# _ENCODE_FILE_NEW=UTF-8

tar -zcvf file.tar.gz file.txt
chtag -r file.txt
ls -lT file.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 8 Jun 15 10:04 file.txt

tar -zcvf file.tar.gz file.txt
# Output:
# file.txt

ls -lT file.txt file.tar.gz
# Output:
# t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 335 Jun 15 10:05 file.tar.gz
# t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR   8 Jun 15 10:04 file.txt

unset _ENCODE_FILE_NEW
unset _ENCODE_FILE_EXISTING
```

**Result:** The archive created by `tar` is tagged with `_ENCODE_FILE_NEW`, and when `tar` writes to an existing untagged input file context, the file is retagged with `_ENCODE_FILE_EXISTING`.

---

### 8. Zip

Archive utilities like `zip` also follow the tagging conventions.

```bash
export _ENCODE_FILE_NEW=UTF-8
export _ENCODE_FILE_EXISTING=IBM-1047
touch out.txt
ls -lT out.txt
# Output: t UTF-8       T=on  -rw-rw-r--  1 SABITHA SYSPGMR 0 Jun 15 03:24 out.txt

echo "hello" > out.txt
env | grep _ENCODE_FILE
# Output:
# _ENCODE_FILE_EXISTING=IBM-1047
# _ENCODE_FILE_NEW=UTF-8

chtag -r out.txt
ls -lT out.txt
# Output: - untagged    T=off -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 15 03:24 out.txt

zip out.zip out.txt
# Output: adding: out.txt (deflated -116%)

ls -lT out.txt
# Output: t IBM-1047    T=on  -rw-rw-r--  1 SABITHA SYSPGMR 6 Jun 15 03:24 out.txt
```

**Result:** The `zip` command writes to the untagged file, applying the IBM-1047 tag from `_ENCODE_FILE_EXISTING`.

---

## Best Practices

1. **Set Both Variables**: Always set both `_ENCODE_FILE_NEW` and `_ENCODE_FILE_EXISTING` for consistent behavior:
   ```bash
   export _ENCODE_FILE_NEW=UTF-8
   export _ENCODE_FILE_EXISTING=UTF-8
   ```

2. **Common Encoding Choices**:
   - **UTF-8**: For modern text files and cross-platform compatibility
   - **ISO8859-1**: For ASCII-compatible text
   - **IBM-1047**: For EBCDIC text (native z/OS encoding)

3. **Git Repositories**: Always explicitly tag files in git repositories:
   ```bash
   chtag -tc ISO8859-1 *.txt  # For text files
   ```

4. **Verify Tags**: Use `ls -lT` to check file tags:
   ```bash
   ls -lT filename
   ```

5. **Clean Up**: Unset variables when no longer needed:
   ```bash
   unset _ENCODE_FILE_NEW
   unset _ENCODE_FILE_EXISTING
   ```

---

## Summary

`_ENCODE_FILE_EXISTING` provides automatic tagging for existing untagged files when they are written to, ensuring proper encoding handling on z/OS. This is particularly important for:

- Downloaded files (curl, wget)
- Shell redirections (>, >>)
- Application output files
- Archive operations

The key distinction is:
- **`_ENCODE_FILE_NEW`**: Tags newly created files
- **`_ENCODE_FILE_EXISTING`**: Tags existing untagged files when written to
- **Already tagged files**: Preserve their existing tags

This mechanism helps maintain encoding consistency across z/OS applications while providing flexibility for different use cases.
