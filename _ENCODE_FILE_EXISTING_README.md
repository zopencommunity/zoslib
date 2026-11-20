# _ENCODE_FILE_EXISTING Environment Variable Documentation

## Overview

The `_ENCODE_FILE_EXISTING` environment variable controls how existing files are tagged/encoded when opened by applications using zoslib on z/OS systems. This variable affects file encoding behavior for various command-line tools and applications.

## Purpose

When an existing file is opened, `_ENCODE_FILE_EXISTING` determines:
- Whether the file should be treated as binary data
- What character encoding (codeset) should be applied to the file

## Syntax

```bash
export _ENCODE_FILE_EXISTING=<value>
```

## Valid Values

- `BINARY` - Treat existing files as binary (no text conversion)
- `<codeset>` - Any valid codeset name (e.g., `ISO8859-1`, `UTF-8`, `IBM-1047`)

## Default Behavior

If `_ENCODE_FILE_EXISTING` is not set, the system uses default file tagging behavior without forcing a specific encoding on existing files.

## Verifying File Tags with ls -T

The `ls -T` command displays file tag information on z/OS, showing the CCSID (Coded Character Set Identifier) and text/binary status of files. This is useful for verifying how `_ENCODE_FILE_EXISTING` affects file handling.

### Example: Viewing File Tags Before Setting Environment Variable

```bash
# Check file tags without _ENCODE_FILE_EXISTING set
$ ls -T existing_file.txt
t ISO8859-1   T=on  existing_file.txt
```

**Output explanation:**
- `t` = text file
- `ISO8859-1` = current codeset/CCSID
- `T=on` = automatic conversion enabled

### Example: File Tags After Setting _ENCODE_FILE_EXISTING=BINARY

```bash
# Set environment variable
$ export _ENCODE_FILE_EXISTING=BINARY

# When the file is opened by an application, it will be treated as binary
# The ls -T output shows the original tag, but applications will handle it as binary
$ ls -T existing_file.txt
t ISO8859-1   T=on  existing_file.txt

# After processing with a tool that respects _ENCODE_FILE_EXISTING
$ cat existing_file.txt > output.txt
$ ls -T output.txt
b binary      T=off output.txt
```

**Output explanation:**
- `b` = binary file (changed from text)
- `binary` = no codeset (binary mode)
- `T=off` = automatic conversion disabled

### Example: File Tags After Setting _ENCODE_FILE_EXISTING=UTF-8

```bash
# Set environment variable to UTF-8
$ export _ENCODE_FILE_EXISTING=UTF-8

# Original file tag
$ ls -T existing_file.txt
t ISO8859-1   T=on  existing_file.txt

# After processing, new files may be tagged with UTF-8
$ cat existing_file.txt > output_utf8.txt
$ ls -T output_utf8.txt
t UTF-8       T=on  output_utf8.txt
```

**Output explanation:**
- File remains text (`t`)
- Codeset changed to `UTF-8`
- Automatic conversion remains enabled (`T=on`)

### Example: Comparing Multiple Files

```bash
$ export _ENCODE_FILE_EXISTING=BINARY

# Check tags for multiple files
$ ls -T *.txt
t ISO8859-1   T=on  file1.txt
t UTF-8       T=on  file2.txt
b binary      T=off file3.txt

# After processing with BINARY mode
$ for f in *.txt; do cat "$f" > "processed_$f"; done
$ ls -T processed_*.txt
b binary      T=off processed_file1.txt
b binary      T=off processed_file2.txt
b binary      T=off processed_file3.txt
```

## Usage Examples with Common Tools

### 1. Bash Commands

#### Example 1: Reading files as binary
```bash
# Set existing files to be treated as binary
export _ENCODE_FILE_EXISTING=BINARY

# Check original file tags
ls -T existing_file.txt source.dat

# Read a file without encoding conversion
cat existing_file.txt

# Copy files preserving binary format
cp source.dat destination.dat

# Check file tags after operations
ls -T existing_file.txt source.dat destination.dat
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ ls -T existing_file.txt source.dat
t ISO8859-1   T=on  existing_file.txt
b binary      T=off source.dat
$ cat existing_file.txt
[Binary data displayed without encoding conversion]
$ cp source.dat destination.dat
$ echo $?
0
$ ls -T existing_file.txt source.dat destination.dat
t ISO8859-1   T=on  existing_file.txt
b binary      T=off source.dat
b binary      T=off destination.dat
```

#### Example 2: Reading files with specific encoding
```bash
# Set existing files to use UTF-8 encoding
export _ENCODE_FILE_EXISTING=UTF-8

# Check original file tags
ls -T existing_file.txt

# Process text files with UTF-8 encoding
while IFS= read -r line; do
    echo "Processing: $line"
done < existing_file.txt
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=UTF-8
$ ls -T existing_file.txt
t ISO8859-1   T=on  existing_file.txt
$ while IFS= read -r line; do echo "Processing: $line"; done < existing_file.txt
Processing: First line of text
Processing: Second line of text
Processing: Third line of text
```

### 2. cURL Commands

#### Example 1: Download files as binary
```bash
# Ensure downloaded files are treated as binary
export _ENCODE_FILE_EXISTING=BINARY

# Download a binary file
curl -o downloaded_file.zip https://example.com/file.zip

# Check file tags after download
ls -T downloaded_file.zip

# Download and verify binary integrity
curl -o image.png https://example.com/image.png

# Check file tags
ls -T image.png
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ curl -o downloaded_file.zip https://example.com/file.zip
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 1024k  100 1024k    0     0  2048k      0 --:--:-- --:--:-- --:--:-- 2048k
$ ls -T downloaded_file.zip
b binary      T=off downloaded_file.zip
$ curl -o image.png https://example.com/image.png
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  256k  100  256k    0     0   512k      0 --:--:-- --:--:-- --:--:--  512k
$ ls -T image.png
b binary      T=off image.png
```

#### Example 2: Download text files with encoding
```bash
# Set encoding for text file downloads
export _ENCODE_FILE_EXISTING=ISO8859-1

# Download and process text content
curl https://example.com/data.txt | grep "pattern"

# Download API response as text
curl -o response.json https://api.example.com/data

# Check file tags
ls -T response.json
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=ISO8859-1
$ curl https://example.com/data.txt | grep "pattern"
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  2048  100  2048    0     0   4096      0 --:--:-- --:--:-- --:--:--  4096
line containing pattern match
another line with pattern
$ curl -o response.json https://api.example.com/data
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100   512  100   512    0     0   1024      0 --:--:-- --:--:-- --:--:--  1024
$ ls -T response.json
t ISO8859-1   T=on  response.json
```

### 3. sed Commands

#### Example 1: Process binary files
```bash
# Treat files as binary to avoid encoding issues
export _ENCODE_FILE_EXISTING=BINARY

# Check original file tags
ls -T binary_data.dat

# Perform binary-safe replacements
sed 's/old/new/g' binary_data.dat > modified.dat

# Check file tags after processing
ls -T binary_data.dat modified.dat
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ ls -T binary_data.dat
b binary      T=off binary_data.dat
$ sed 's/old/new/g' binary_data.dat > modified.dat
$ echo $?
0
$ ls -T binary_data.dat modified.dat
b binary      T=off binary_data.dat
b binary      T=off modified.dat
```

#### Example 2: Process text files with encoding
```bash
# Set encoding for text processing
export _ENCODE_FILE_EXISTING=UTF-8

# Check original file tags
ls -T existing_file.txt input.txt

# Edit text files with proper encoding
sed -i 's/pattern/replacement/g' existing_file.txt

# Stream editing with encoding awareness
sed 's/foo/bar/g' input.txt > output.txt

# Check file tags after processing
ls -T existing_file.txt input.txt output.txt
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=UTF-8
$ ls -T existing_file.txt input.txt
t ISO8859-1   T=on  existing_file.txt
t ISO8859-1   T=on  input.txt
$ sed -i 's/pattern/replacement/g' existing_file.txt
$ cat existing_file.txt
This line has replacement instead of pattern
Another line with replacement
$ sed 's/foo/bar/g' input.txt > output.txt
$ cat output.txt
This line has bar instead of foo
Another bar line
$ ls -T existing_file.txt input.txt output.txt
t UTF-8       T=on  existing_file.txt
t ISO8859-1   T=on  input.txt
t UTF-8       T=on  output.txt
```

### 4. GPG Commands

#### Example 1: Encrypt/decrypt binary files
```bash
# Ensure binary handling for encrypted files
export _ENCODE_FILE_EXISTING=BINARY

# Check original file tags
ls -T encrypted_file.gpg binary_file.dat

# Decrypt a binary file
gpg --decrypt encrypted_file.gpg > decrypted_file.bin

# Encrypt a binary file
gpg --encrypt --recipient user@example.com binary_file.dat

# Check file tags after operations
ls -T encrypted_file.gpg binary_file.dat decrypted_file.bin binary_file.dat.gpg
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ ls -T encrypted_file.gpg binary_file.dat
b binary      T=off encrypted_file.gpg
b binary      T=off binary_file.dat
$ gpg --decrypt encrypted_file.gpg > decrypted_file.bin
gpg: encrypted with 2048-bit RSA key, ID 12345678, created 2026-01-01
      "User Name <user@example.com>"
$ gpg --encrypt --recipient user@example.com binary_file.dat
$ ls -T encrypted_file.gpg binary_file.dat decrypted_file.bin binary_file.dat.gpg
b binary      T=off encrypted_file.gpg
b binary      T=off binary_file.dat
b binary      T=off decrypted_file.bin
b binary      T=off binary_file.dat.gpg
```

#### Example 2: Sign and verify text files
```bash
# Set encoding for text file operations
export _ENCODE_FILE_EXISTING=UTF-8

# Check original file tags
ls -T document.txt

# Sign a text file
gpg --clearsign document.txt

# Verify a signed text file
gpg --verify document.txt.asc

# Check file tags after signing
ls -T document.txt document.txt.asc
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=UTF-8
$ ls -T document.txt
t ISO8859-1   T=on  document.txt
$ gpg --clearsign document.txt
$ cat document.txt.asc
-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA256

This is the document content
-----BEGIN PGP SIGNATURE-----

iQEzBAEBCAAdFiEE...
-----END PGP SIGNATURE-----
$ gpg --verify document.txt.asc
gpg: Signature made Mon May 25 10:45:00 2026 EDT
gpg:                using RSA key 12345678ABCDEF
gpg: Good signature from "User Name <user@example.com>" [ultimate]
$ ls -T document.txt document.txt.asc
t ISO8859-1   T=on  document.txt
t UTF-8       T=on  document.txt.asc
```

### 5. zip/unzip Commands

#### Example 1: Create and extract binary archives
```bash
# Treat archive contents as binary
export _ENCODE_FILE_EXISTING=BINARY

# Check original file tags
ls -T directory/file1.bin directory/file2.dat

# Create a zip archive
zip -r archive.zip directory/

# Extract preserving binary format
unzip archive.zip

# List archive contents
unzip -l archive.zip

# Check file tags after extraction
ls -T archive.zip directory/file1.bin directory/file2.dat
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ ls -T directory/file1.bin directory/file2.dat
b binary      T=off directory/file1.bin
b binary      T=off directory/file2.dat
$ zip -r archive.zip directory/
  adding: directory/ (stored 0%)
  adding: directory/file1.bin (deflated 45%)
  adding: directory/file2.dat (deflated 52%)
$ unzip archive.zip
Archive:  archive.zip
   creating: directory/
  inflating: directory/file1.bin
  inflating: directory/file2.dat
$ unzip -l archive.zip
Archive:  archive.zip
  Length      Date    Time    Name
---------  ---------- -----   ----
        0  05-25-2026 10:50   directory/
     4096  05-25-2026 10:50   directory/file1.bin
     8192  05-25-2026 10:50   directory/file2.dat
---------                     -------
    12288                     3 files
$ ls -T archive.zip directory/file1.bin directory/file2.dat
b binary      T=off archive.zip
b binary      T=off directory/file1.bin
b binary      T=off directory/file2.dat
```

#### Example 2: Handle text files in archives
```bash
# Set encoding for text file handling
export _ENCODE_FILE_EXISTING=ISO8859-1

# Check original file tags
ls -T file1.txt file2.txt file3.txt

# Create archive with text files
zip -r text_archive.zip *.txt

# Extract and convert text files
unzip text_archive.zip

# Check file tags after extraction
ls -T text_archive.zip file1.txt file2.txt file3.txt
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=ISO8859-1
$ ls -T file1.txt file2.txt file3.txt
t UTF-8       T=on  file1.txt
t UTF-8       T=on  file2.txt
t UTF-8       T=on  file3.txt
$ zip -r text_archive.zip *.txt
  adding: file1.txt (deflated 35%)
  adding: file2.txt (deflated 42%)
  adding: file3.txt (deflated 38%)
$ unzip text_archive.zip
Archive:  text_archive.zip
  inflating: file1.txt
  inflating: file2.txt
  inflating: file3.txt
$ ls -T text_archive.zip file1.txt file2.txt file3.txt
b binary      T=off text_archive.zip
t ISO8859-1   T=on  file1.txt
t ISO8859-1   T=on  file2.txt
t ISO8859-1   T=on  file3.txt
$ cat file1.txt
Text content with ISO8859-1 encoding
```

### 6. tar Commands

#### Example 1: Create and extract binary tarballs
```bash
# Ensure binary handling for tar operations
export _ENCODE_FILE_EXISTING=BINARY

# Check original file tags
ls -T directory/file1.bin directory/file2.dat directory/subdir/file3.bin

# Create a tar archive
tar -czf archive.tar.gz directory/

# Extract tar archive
tar -xzf archive.tar.gz

# List tar contents
tar -tzf archive.tar.gz

# Check file tags after extraction
ls -T archive.tar.gz directory/file1.bin directory/file2.dat directory/subdir/file3.bin
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ ls -T directory/file1.bin directory/file2.dat directory/subdir/file3.bin
b binary      T=off directory/file1.bin
b binary      T=off directory/file2.dat
b binary      T=off directory/subdir/file3.bin
$ tar -czf archive.tar.gz directory/
$ tar -xzf archive.tar.gz
$ tar -tzf archive.tar.gz
directory/
directory/file1.bin
directory/file2.dat
directory/subdir/
directory/subdir/file3.bin
$ ls -T archive.tar.gz directory/file1.bin directory/file2.dat directory/subdir/file3.bin
b binary      T=off archive.tar.gz
b binary      T=off directory/file1.bin
b binary      T=off directory/file2.dat
b binary      T=off directory/subdir/file3.bin
```

#### Example 2: Handle text files in tarballs
```bash
# Set encoding for text file operations
export _ENCODE_FILE_EXISTING=UTF-8

# Check original file tags
ls -T *.log

# Create tar with text files
tar -czf logs.tar.gz *.log

# Extract and process text files
tar -xzf logs.tar.gz

# Check file tags after extraction
ls -T logs.tar.gz extracted.log

# View content
cat extracted.log
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=UTF-8
$ ls -T *.log
t ISO8859-1   T=on  app.log
t ISO8859-1   T=on  error.log
t ISO8859-1   T=on  extracted.log
$ tar -czf logs.tar.gz *.log
$ tar -xzf logs.tar.gz
$ ls -T logs.tar.gz extracted.log
b binary      T=off logs.tar.gz
t UTF-8       T=on  extracted.log
$ cat extracted.log
2026-05-25 11:00:00 INFO Application started
2026-05-25 11:00:01 DEBUG Processing request
2026-05-25 11:00:02 INFO Request completed successfully
```

## Combined Usage Scenarios

### Scenario 1: Download, extract, and process binary data
```bash
export _ENCODE_FILE_EXISTING=BINARY

# Download archive
curl -o data.tar.gz https://example.com/data.tar.gz

# Check file tags
ls -T data.tar.gz

# Extract archive
tar -xzf data.tar.gz

# Check extracted file tags
ls -T binary_data.bin

# Process binary files
cat binary_data.bin | sed 's/\x00/\x01/g' > modified.bin

# Check file tags after processing
ls -T data.tar.gz binary_data.bin modified.bin
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ curl -o data.tar.gz https://example.com/data.tar.gz
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 5120k  100 5120k    0     0  10.2M      0 --:--:-- --:--:-- --:--:-- 10.2M
$ ls -T data.tar.gz
b binary      T=off data.tar.gz
$ tar -xzf data.tar.gz
$ ls -T binary_data.bin
b binary      T=off binary_data.bin
$ cat binary_data.bin | sed 's/\x00/\x01/g' > modified.bin
$ ls -T data.tar.gz binary_data.bin modified.bin
b binary      T=off data.tar.gz
b binary      T=off binary_data.bin
b binary      T=off modified.bin
```

### Scenario 2: Process encrypted text files
```bash
export _ENCODE_FILE_EXISTING=UTF-8

# Check original file tags
ls -T secret.txt.gpg

# Decrypt file
gpg --decrypt secret.txt.gpg > secret.txt

# Check decrypted file tags
ls -T secret.txt

# Process text content
sed 's/sensitive/redacted/g' secret.txt > processed.txt

# Create archive
zip processed.zip processed.txt

# Check file tags after all operations
ls -T secret.txt.gpg secret.txt processed.txt processed.zip
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=UTF-8
$ ls -T secret.txt.gpg
b binary      T=off secret.txt.gpg
$ gpg --decrypt secret.txt.gpg > secret.txt
gpg: encrypted with 2048-bit RSA key, ID 12345678, created 2026-01-01
      "User Name <user@example.com>"
$ ls -T secret.txt
t UTF-8       T=on  secret.txt
$ sed 's/sensitive/redacted/g' secret.txt > processed.txt
$ cat processed.txt
This document contains redacted information
All redacted data has been removed
$ zip processed.zip processed.txt
  adding: processed.txt (deflated 28%)
$ ls -T secret.txt.gpg secret.txt processed.txt processed.zip
b binary      T=off secret.txt.gpg
t UTF-8       T=on  secret.txt
t UTF-8       T=on  processed.txt
b binary      T=off processed.zip
```

### Scenario 3: Backup and restore with encoding
```bash
# For backup (binary mode)
export _ENCODE_FILE_EXISTING=BINARY

# Check original file tags
ls -T /path/to/data/config.txt /path/to/data/data.txt /path/to/data/logs.txt

# Create backup
tar -czf backup.tar.gz /path/to/data

# Check backup file tags
ls -T backup.tar.gz

# For restore (with encoding)
export _ENCODE_FILE_EXISTING=ISO8859-1
tar -xzf backup.tar.gz

# Check restored file tags
ls -T backup.tar.gz path/to/data/config.txt path/to/data/data.txt path/to/data/logs.txt
```

**Output:**
```
$ export _ENCODE_FILE_EXISTING=BINARY
$ ls -T /path/to/data/config.txt /path/to/data/data.txt /path/to/data/logs.txt
t UTF-8       T=on  /path/to/data/config.txt
t UTF-8       T=on  /path/to/data/data.txt
t UTF-8       T=on  /path/to/data/logs.txt
$ tar -czf backup.tar.gz /path/to/data
$ ls -T backup.tar.gz
b binary      T=off backup.tar.gz
$ export _ENCODE_FILE_EXISTING=ISO8859-1
$ tar -xzf backup.tar.gz
$ ls -T backup.tar.gz path/to/data/config.txt path/to/data/data.txt path/to/data/logs.txt
b binary      T=off backup.tar.gz
t ISO8859-1   T=on  path/to/data/config.txt
t ISO8859-1   T=on  path/to/data/data.txt
t ISO8859-1   T=on  path/to/data/logs.txt
```

## Best Practices

1. **Binary Files**: Always use `_ENCODE_FILE_EXISTING=BINARY` for:
   - Compressed archives (zip, tar.gz)
   - Encrypted files
   - Binary executables
   - Image files
   - Media files

2. **Text Files**: Use appropriate codeset for:
   - Log files
   - Configuration files
   - Source code
   - Documentation

3. **Consistency**: Set the variable at the beginning of scripts to ensure consistent behavior throughout execution.

4. **Verification**: After setting the variable, verify file operations produce expected results.

## Troubleshooting

### Issue: File corruption after processing
**Solution**: Ensure `_ENCODE_FILE_EXISTING=BINARY` is set for binary files.

### Issue: Text encoding problems
**Solution**: Verify the correct codeset is specified (e.g., UTF-8, ISO8859-1).

### Issue: Inconsistent behavior
**Solution**: Check if the variable is set globally or only in specific contexts.

## Related Environment Variables

- `_ENCODE_FILE_NEW` - Controls encoding for newly created files
- `_TAG_REDIR_ERR` - Controls error stream tagging
- `_TAG_REDIR_IN` - Controls input stream tagging
- `_TAG_REDIR_OUT` - Controls output stream tagging

## Implementation Details

The `_ENCODE_FILE_EXISTING` variable is processed by the `__tag_existing_file()` function in zoslib, which:
1. Checks if the variable is set
2. If set to "BINARY", marks the file descriptor as binary
3. Otherwise, applies the specified codeset to the file descriptor

## References

- zoslib source: `src/zos-io.cc`
- Function: `__tag_existing_file(int fd)`
- Related functions: `__setfdbinary()`, `__chgfdcodeset()`
