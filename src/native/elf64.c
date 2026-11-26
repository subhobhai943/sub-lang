/* ========================================
   SUB Language - static ELF64 writer
   ----------------------------------------
   Produces a position-dependent, statically linked x86-64 Linux executable
   with no dynamic segment and no section headers. The kernel needs only the
   ELF header, the program headers and the loadable segments; everything a
   linker would normally add is either resolved at emit time (all addresses
   are absolute immediates) or not needed at all (no relocations, no symbols).

   Two segments are written:

     R|X  the headers plus the text buffer, mapped at X64_TEXT_BASE
     R|W  an anonymous zero-filled region at X64_BSS_BASE, which holds the
          heap pointer, the formatting scratch buffer and the bump heap

   The second segment has p_filesz 0 and a large p_memsz, so the 64 MB heap
   costs nothing on disk and is faulted in lazily.
   ======================================== */

#include "x64.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define EHDR_SIZE   64
#define PHDR_SIZE   56
#define PHDR_COUNT  2
#define HDR_TOTAL   (EHDR_SIZE + PHDR_SIZE * PHDR_COUNT)

#define PT_LOAD     1
#define PF_X        1
#define PF_W        2
#define PF_R        4

size_t elf64_text_offset(void) { return HDR_TOTAL; }

static void put_phdr(Buf *out, uint32_t type, uint32_t flags,
                     uint64_t offset, uint64_t vaddr,
                     uint64_t filesz, uint64_t memsz, uint64_t align) {
    buf_u32(out, type);
    buf_u32(out, flags);
    buf_u64(out, offset);
    buf_u64(out, vaddr);
    buf_u64(out, vaddr);      /* p_paddr, ignored for executables */
    buf_u64(out, filesz);
    buf_u64(out, memsz);
    buf_u64(out, align);
}

int elf64_write_target(const char *path, const Buf *text, size_t entry_off,
                       uint16_t e_machine, uint64_t text_base,
                       uint64_t bss_base, uint64_t bss_size) {
    Buf out;
    buf_init(&out);

    uint64_t entry = text_base + HDR_TOTAL + entry_off;

    /* --- ELF header --- */
    static const uint8_t ident[16] = {
        0x7F, 'E', 'L', 'F',
        2,      /* ELFCLASS64 */
        1,      /* ELFDATA2LSB */
        1,      /* EV_CURRENT */
        0,      /* ELFOSABI_SYSV */
        0, 0, 0, 0, 0, 0, 0, 0
    };
    buf_bytes(&out, ident, 16);
    buf_u16(&out, 2);            /* e_type    = ET_EXEC */
    buf_u16(&out, e_machine);    /* EM_X86_64=62, EM_AARCH64=183 */
    buf_u32(&out, 1);            /* e_version */
    buf_u64(&out, entry);
    buf_u64(&out, EHDR_SIZE);    /* e_phoff */
    buf_u64(&out, 0);            /* e_shoff: no section headers */
    buf_u32(&out, 0);            /* e_flags */
    buf_u16(&out, EHDR_SIZE);
    buf_u16(&out, PHDR_SIZE);
    buf_u16(&out, PHDR_COUNT);
    buf_u16(&out, 64);           /* e_shentsize */
    buf_u16(&out, 0);            /* e_shnum */
    buf_u16(&out, 0);            /* e_shstrndx */

    /* --- Program headers --- */
    uint64_t text_filesz = HDR_TOTAL + text->len;
    put_phdr(&out, PT_LOAD, PF_R | PF_X,
             0, text_base, text_filesz, text_filesz, 0x1000);
    put_phdr(&out, PT_LOAD, PF_R | PF_W,
             0, bss_base, 0, bss_size, 0x1000);

    /* --- Text --- */
    buf_bytes(&out, text->data, text->len);

    size_t total = out.len;
    FILE *f = fopen(path, "wb");
    if (!f) { buf_free(&out); return -1; }
    size_t wrote = fwrite(out.data, 1, total, f);
    int closed_ok = (fclose(f) == 0);
    buf_free(&out);
    if (wrote != total || !closed_ok) return -1;

#ifndef _WIN32
    /* An executable the user cannot execute is not an executable. Windows
       has no execute bit, and this backend does not target it anyway; the
       file still has to compile there because subc links it in. */
    if (chmod(path, 0755) != 0) return -1;
#endif
    return 0;
}

int elf64_write(const char *path, const Buf *text, size_t entry_off) {
    return elf64_write_target(path, text, entry_off, 0x3E,
                              X64_TEXT_BASE, X64_BSS_BASE, X64_BSS_SIZE);
}
