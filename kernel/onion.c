#include "gdt.h"
#include "idt.h"
#include "vga.h"
#include "vbe.h"
#include "serial.h"
#include "font.h"
#include "console.h"
#include "pic.h"
#include "syscall.h"
#include "vfs.h"
#include "process.h"
#include "syscall.h"
#include "pmm.h"
#include "paging.h"
#include "vmm.h"

//pineaplle zone 1

#define MULTIBOOT_MAGIC_EXPECTED 0x2badb002

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long long uint64_t;

typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
} __attribute__((packed)) multiboot_info_t;

typedef struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed)) multiboot_mod_list_t;

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

extern void xv6fs_full_init(uint8_t *ramdisk, unsigned int ramdisk_len);
extern int  xv6fs_bridge_read_file(const char *path, uint8_t *out, unsigned int maxlen);

void panic(const char *message) {
    asm volatile ("cli");
    kprint("PANIC: ", 0x00FF0000);
    kprint((char *)message, 0x00000000);
    kprint("\nSystem halted.\n", 0x00000000);
    for (;;) {
        asm volatile ("hlt");
    }
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void init_timer(uint32_t freq) {
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t load_elf(uint8_t *file_start) {
    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)file_start;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
        panic("Invalid ELF");
    }

    kprint("Valid 64-bit ELF loading segments...\n", 0x00000000);

    elf64_phdr_t *phdr = (elf64_phdr_t *)(file_start + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == 1) { // PT_LOAD
            uint8_t *dest = (uint8_t *)phdr[i].p_vaddr;
            uint8_t *src  = file_start + phdr[i].p_offset;

            for (uint64_t j = 0; j < phdr[i].p_filesz; j++) {
                dest[j] = src[j];
            }
            for (uint64_t j = phdr[i].p_filesz; j < phdr[i].p_memsz; j++) {
                dest[j] = 0;
            }
        }
    }

    return ehdr->e_entry;
}

static void
xv6fs_mount_and_test(multiboot_info_t *mb_info)
{
    if (!(mb_info->flags & (1 << 3)) || mb_info->mods_count < 2) {
        kprint("xv6fs: no fs.img module found (need 2nd multiboot module), skipping\n", 0x00FF0000);
        return;
    }

    multiboot_mod_list_t *mods = (multiboot_mod_list_t *)(uint64_t)mb_info->mods_addr;
    multiboot_mod_list_t *fs_mod = &mods[1];

    uint8_t  *fs_base = (uint8_t *)(uint64_t)fs_mod->mod_start;
    uint32_t  fs_size  = fs_mod->mod_end - fs_mod->mod_start;

    kprint("xv6fs: mounting fs.img...\n", 0x00000000);

    xv6fs_full_init(fs_base, fs_size);

    kprint("xv6fs: mounted /README...\n", 0x00000000);

    uint8_t buf[512];
    int n = xv6fs_bridge_read_file("/README", buf, sizeof(buf) - 1);

    if (n < 0) {
        kprint("xv6fs: TEST READ FAILED\n", 0x00FF0000);
        return;
    }

    buf[n] = 0;
    kprint("xv6fs: TEST READ OK, content:\n", 0x00000000);
    kprint((char *)buf, 0x00000000);
    kprint("\n", 0x00000000);
}

void kernel(uint64_t magic, uint64_t mb_info_addr) {
    serinit();
    pmm_init(512); 
    kprint("PMMI\n", 0x00000000);

    paging_init();
    paging_enable();
    kprint("PON\n", 0x00000000);
    vmm_init();
    kprint("VMMI\n", 0x00000000);
    iinit();       // IDT
    picremap();
    syscall_init();
    vbemode(800, 600, 32);
    extern uint32_t pcibar0(void);
    uint32_t addr = pcibar0();
    serwrite("FB addr :\n");
    for (int shift = 28; shift >= 0; shift -= 4) {
        int nibble = (addr >> shift) & 0xF;
        char c = nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10);
        char buf[2] = {c, 0};
        serwrite(buf);
    }
    serwrite("\n");

    vbefill(0x00FFFFFF);
    cinit();

    kprint("64-bit Kernel\n", 0x00000000);

    if (magic == MULTIBOOT_MAGIC_EXPECTED) {
        multiboot_info_t *mb_info = (multiboot_info_t *)mb_info_addr;

        if ((mb_info->flags & (1 << 3)) && (mb_info->mods_count > 0)) {
            multiboot_mod_list_t *mod = (multiboot_mod_list_t *)(uint64_t)mb_info->mods_addr;
            uint8_t *elf_image = (uint8_t *)(uint64_t)mod->mod_start;

            kprint("Found binary module\n", 0x00000000);

            uint64_t entry_point = load_elf(elf_image);
            kprint("ELF loaded\n", 0x00000000);

            xv6fs_mount_and_test(mb_info);

            scheduler_init();
            kprint("Scheduler initialized\n", 0x00000000);

            create_process(entry_point);
            kprint("Process created\n", 0x00000000);

            init_timer(100);
            kprint("Timer initialized\n", 0x00000000);

            uint8_t current_mask = inb(0x21);
            serwrite("PIC1 Mask before STI: ");
            char hex_buf[3] = { "0123456789ABCDEF"[(current_mask >> 4) & 0xF], "0123456789ABCDEF"[current_mask & 0xF], '\n' };
            serwrite(hex_buf);

            asm volatile ("sti");
            kprint("sti done :)\n", 0x00000000);

        } else {
            panic("No init ELF module found!");
        }
    } else {
        panic("Invalid Multiboot magic number!");
    }

    for (;;) {
        asm volatile ("hlt");
    }
}
