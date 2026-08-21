#include "ramfs.h"
#include "console.h"

extern uint64_t load_elf(const uint8_t *elf_data);
extern void create_process(uint64_t entry_point);

int spawn_from_ramfs(const char *filename) {
    int fd = ramfs_open(filename);
    if (fd < 0) {
        kprint("[RAMFS] File not found: ", 0x00FF0000);
        kprint(filename, 0x00FF0000);
        kprint("\n", 0x00FF0000);
        return -1;
    }

    const uint8_t *elf_data = ramfs_data(fd);
    uint64_t entry_point = load_elf(elf_data);
    
    if (!entry_point) {
        kprint("[RAMFS] Failed to parse ELF: ", 0x00FF0000);
        kprint(filename, 0x00FF0000);
        kprint("\n", 0x00FF0000);
        return -1;
    }

    create_process(entry_point);
    kprint("[RAMFS] Process spawned: ", 0x0000FF00);
    kprint(filename, 0x0000FF00);
    kprint("\n", 0x0000FF00);
    return 0;
}