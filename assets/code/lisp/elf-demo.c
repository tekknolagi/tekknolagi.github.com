// vim: set tabstop=2 shiftwidth=2 textwidth=79 expandtab:
#include <err.h>
#include <fcntl.h>
#include <libelf.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define LOADADDR 0x08048000

// clang-format off
unsigned char code[] = {
    // mov eax, 42 (0x2a)
    0xb8, 0x2a, 0x00, 0x00, 0x00,
    // ret
    0xc3,
};

char string_table[] = {
  /* Offset 0 */ '\0',
  /* Offset 1 */ '.', 'f', 'o', 'o', '\0',
  /* Offset 6 */ '.', 's', 'h', 's', 't', 'r', 't', 'a', 'b', '\0',
  /* Offset 16 */ 'm', 'a', 'i', 'n', '\0',
  /* Offset 21 */ '.', 's', 'y', 'm', 't', 'a', 'b', '\0',
};

// clang-format on

int main(int argc, char **argv) {
  if (argc != 2) {
    errx(EXIT_FAILURE, "usage: %s file-name", argv[0]);
  }
  if (elf_version(EV_CURRENT) == EV_NONE) {
    errx(EXIT_FAILURE, "ELF library initialization failed: %s", elf_errmsg(-1));
  }
  int fd;
  if ((fd = open(argv[1], O_WRONLY | O_CREAT, 0777)) < 0) {
    err(EXIT_FAILURE, "open '%s' failed", argv[1]);
  }
  Elf *e;
  if ((e = elf_begin(fd, ELF_C_WRITE, NULL)) == NULL) {
    errx(EXIT_FAILURE, "elf_begin() failed: %s", elf_errmsg(-1));
  }
  Elf64_Ehdr *ehdr;
  if ((ehdr = elf64_newehdr(e)) == NULL) {
    errx(EXIT_FAILURE, "elf64_newehdr() failed: %s", elf_errmsg(-1));
  }

  // size_t ehdrsz = elf64_fsize(ELF_T_EHDR, 1, EV_CURRENT);
  // size_t phdrsz = elf64_fsize(ELF_T_PHDR, 1, EV_CURRENT);

  ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
  ehdr->e_ident[EI_CLASS] = ELFCLASS64;
  ehdr->e_machine = EM_X86_64;
  ehdr->e_type = ET_REL;
  ehdr->e_entry = 0; // LOADADDR + ehdrsz + phdrsz;

  uint16_t code_section_index;

  {
    Elf_Scn *section;
    if ((section = elf_newscn(e)) == NULL) {
      errx(EXIT_FAILURE, "elf_newscn() failed: %s", elf_errmsg(-1));
    }
    Elf_Data *data;
    if ((data = elf_newdata(section)) == NULL) {
      errx(EXIT_FAILURE, "elf_newdata() failed: %s", elf_errmsg(-1));
    }

    data->d_align = 8;
    data->d_off = 0LL;
    data->d_buf = code;
    data->d_type = ELF_T_BYTE;
    data->d_size = sizeof code;
    data->d_version = EV_CURRENT;

    code_section_index = elf_ndxscn(section);

    Elf64_Shdr *shdr;
    if ((shdr = elf64_getshdr(section)) == NULL) {
      errx(EXIT_FAILURE, "elf64_getshdr() failed: %s", elf_errmsg(-1));
    }

    shdr->sh_name = 16;
    shdr->sh_type = SHT_PROGBITS;
    shdr->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdr->sh_entsize = 0;
    shdr->sh_addr = 0; // LOADADDR + ehdrsz + phdrsz;
  }

  {
    Elf_Scn *section;
    if ((section = elf_newscn(e)) == NULL) {
      errx(EXIT_FAILURE, "elf_newscn() failed: %s", elf_errmsg(-1));
    }
    Elf_Data *data;
    if ((data = elf_newdata(section)) == NULL) {
      errx(EXIT_FAILURE, "elf_newdata() failed: %s", elf_errmsg(-1));
    }

    data->d_align = 1;
    data->d_buf = string_table;
    data->d_off = 0LL;
    data->d_size = sizeof string_table;
    data->d_type = ELF_T_BYTE;
    data->d_version = EV_CURRENT;

    ehdr->e_shstrndx = elf_ndxscn(section);

    Elf64_Shdr *shdr;
    if ((shdr = elf64_getshdr(section)) == NULL) {
      errx(EXIT_FAILURE, "elf64_getshdr() failed: %s", elf_errmsg(-1));
    }

    shdr->sh_name = 6;
    shdr->sh_type = SHT_STRTAB;
    shdr->sh_flags = SHF_STRINGS | SHF_ALLOC;
    shdr->sh_entsize = 0;
  }

  {
    Elf_Scn *section;
    if ((section = elf_newscn(e)) == NULL) {
      errx(EXIT_FAILURE, "elf_newscn() failed: %s", elf_errmsg(-1));
    }
    Elf_Data *data;
    if ((data = elf_newdata(section)) == NULL) {
      errx(EXIT_FAILURE, "elf_newdata() failed: %s", elf_errmsg(-1));
    }

    Elf64_Sym symtab[] = {
        {.st_name = 1,
         .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
         .st_other = ELF64_ST_VISIBILITY(STV_DEFAULT),
         .st_shndx = code_section_index,
         .st_value = 0, /* address? */
         .st_size = sizeof code},
    };

    data->d_align = 1;
    data->d_buf = symtab;
    data->d_off = 0LL;
    data->d_size = sizeof symtab;
    data->d_type = ELF_T_SYM;
    data->d_version = EV_CURRENT;

    Elf64_Shdr *shdr;
    if ((shdr = elf64_getshdr(section)) == NULL) {
      errx(EXIT_FAILURE, "elf64_getshdr() failed: %s", elf_errmsg(-1));
    }

    shdr->sh_name = 21;
    shdr->sh_type = SHT_SYMTAB;
    shdr->sh_flags = SHF_STRINGS | SHF_ALLOC;
    shdr->sh_entsize = 0;
  }

  if (elf_update(e, ELF_C_NULL) < 0) {
    errx(EXIT_FAILURE, "elf_update(NULL) failed: %s", elf_errmsg(-1));
  }

  // {
  //   Elf64_Phdr *phdr;
  //   if ((phdr = elf64_newphdr(e, 1)) == NULL) {
  //     errx(EXIT_FAILURE, "elf64_newphdr() failed: %s", elf_errmsg(-1));
  //   }
  //   phdr->p_type = PT_PHDR;
  //   phdr->p_offset = ehdr->e_phoff;
  //   phdr->p_filesz = elf64_fsize(ELF_T_PHDR, 1, EV_CURRENT);
  // }

  //{
  //  Elf64_Phdr *phdr;
  //  if ((phdr = elf64_newphdr(e, 1)) == NULL) {
  //    errx(EXIT_FAILURE, "elf64_newphdr %s\n", elf_errmsg(-1));
  //  }

  //  phdr->p_type = PT_LOAD;
  //  phdr->p_offset = 0;
  //  phdr->p_filesz = ehdrsz + phdrsz + sizeof(code);
  //  phdr->p_memsz = phdr->p_filesz;
  //  phdr->p_vaddr = LOADADDR;
  //  phdr->p_paddr = phdr->p_vaddr;
  //  phdr->p_align = 4;
  //  phdr->p_flags = PF_X | PF_R;
  //}

  elf_flagphdr(e, ELF_C_SET, ELF_F_DIRTY);

  if (elf_update(e, ELF_C_WRITE) < 0) {
    errx(EXIT_FAILURE, "elf_update() failed: %s", elf_errmsg(-1));
  }

  elf_end(e);
  close(fd);

  return EXIT_SUCCESS;
}
