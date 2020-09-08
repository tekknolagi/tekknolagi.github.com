// vim: set tabstop=2 shiftwidth=2 textwidth=79 expandtab:
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// clang-format off
unsigned char code[] = {
  // mov rax, 60
  0x48, 0xc7, 0xc0, 0x3c, 0x00, 0x00, 0x00,
  // mov rdi, 42
  0x48, 0xc7, 0xc7, 0x2a, 0x00, 0x00, 0x00,
  // syscall
  0x0f, 0x05,
};
// clang-format on

int db(int fd, unsigned char value) {
  if (fd == -1)
    return sizeof value;
  return write(fd, &value, 1);
}

static const unsigned kBitsPerByte = 8;

int dw(int fd, uint16_t value) {
  if (fd == -1)
    return sizeof value;
  for (unsigned i = 0; i < sizeof value; i++) {
    db(fd, (value >> (i * kBitsPerByte)) & 0xff);
  }
  return sizeof value;
}

int dd(int fd, uint32_t value) {
  if (fd == -1)
    return sizeof value;
  for (unsigned i = 0; i < sizeof value; i++) {
    db(fd, (value >> (i * kBitsPerByte)) & 0xff);
  }
  return sizeof value;
}

int dq(int fd, uint64_t value) {
  if (fd == -1)
    return sizeof value;
  for (unsigned i = 0; i < sizeof value; i++) {
    db(fd, (value >> (i * kBitsPerByte)) & 0xff);
  }
  return sizeof value;
}

typedef struct offsets {
  int64_t entry;
  int64_t phoff;
  int64_t ehsize;
  int64_t phentsize;
  int64_t filesize;
} offsets;

offsets writeprogram(int fd, int64_t entry, int64_t phoff, int64_t ehsize,
                     int64_t phentsize, int64_t filesize) {
  int32_t org = 0x08048000;
  // ehdr:
  int64_t off = org;
  int64_t ehdr = off;
  off += db(fd, 0x7f); // e_ident
  off += db(fd, 'E');
  off += db(fd, 'L');
  off += db(fd, 'F');
  off += db(fd, 2);
  off += db(fd, 1);
  off += db(fd, 1);
  off += db(fd, 0);
  // times 8 db; padding?
  for (int i = 0; i < 8; i++) {
    off += db(fd, 0);
  }
  off += dw(fd, 2);         // e_type
  off += dw(fd, 62);        // e_machine (x86_64)
  off += dd(fd, 1);         // e_version
  off += dq(fd, entry);     // e_entry: address of _start
  off += dq(fd, phoff);     // e_phoff: relative offset to phdr
  off += dq(fd, 0);         // e_shoff
  off += dd(fd, 0);         // e_flags
  off += dw(fd, ehsize);    // e_ehsize: ehdrsize
  off += dw(fd, phentsize); // e_phentsize: phdrsize
  off += dw(fd, 1);         // e_phnum
  off += dw(fd, 0);         // e_shentsize
  off += dw(fd, 0);         // e_shnum
  off += dw(fd, 0);         // e_shstrndx
  offsets result = {
      .entry = 0, .phoff = 0, .ehsize = 0, .phentsize = 0, .filesize = 0};
  result.ehsize = off - ehdr;

  // phdr:
  result.phoff = off - org;
  int64_t phdr = off;
  off += dd(fd, 1);        // p_type
  off += dd(fd, 5);        // p_flags
  off += dq(fd, 0);        // p_offset
  off += dq(fd, org);      // p_vaddr
  off += dq(fd, org);      // p_paddr
  off += dq(fd, filesize); // p_filesz: filesize
  off += dq(fd, filesize); // p_memsz: filesize
  off += dq(fd, 0x1000);   // p_align
  result.phentsize = off - phdr;

  // _start:
  result.entry = off;
  for (unsigned i = 0; i < sizeof code; i++) {
    off += db(fd, code[i]);
  }

  result.filesize = off - org;
  // assert(result.filesize == 91);
  return result;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    errx(EXIT_FAILURE, "usage: %s file-name", argv[0]);
  }
  const char *filename = argv[1];

  offsets result = writeprogram(-1, -1, -1, -1, -1, -1);

  int fd;
  if ((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777)) < 0) {
    err(EXIT_FAILURE, "open '%s' failed", argv[1]);
  }

  writeprogram(fd, result.entry, result.phoff, result.ehsize, result.phentsize,
               result.filesize);

  close(fd);
  return EXIT_SUCCESS;
}
