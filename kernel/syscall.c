#include "syscall.h"
#include "console.h"
#include "types.h"
#include "vbe.h"
#include "stdint.h"
#include "process.h"
#include "xv6fs/xv6fs_api.h"

typedef uint64_t size_t;

#define PAGE_SIZE 4096
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))
#define VMM_USER_FLAGS 0x07 
extern char kgetc(void);
extern void exit_current_process(void);
extern int spawn_from_ramfs(const char *filename);
extern void *pmm_alloc_page(void);
extern void pmm_free_page(void *ptr);
extern void vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
extern void vmm_unmap_page(uint64_t *pml4, uint64_t virt);
struct inode;
extern struct inode* namei(char *path);
extern void ilock(struct inode *ip);
extern void iunlock(struct inode *ip);
extern void iput(struct inode *ip);
extern int readi(struct inode *ip, char *dst, uint32_t off, uint32_t n);
extern int writei(struct inode *ip, char *src, uint32_t off, uint32_t n);
static uint64_t sys_brk(uint64_t new_brk) {
    process_t *proc = get_current_process();
    if (!proc) return 0;

    if (proc->heap_start == 0) {
        proc->heap_start = 0x700000000000;
        proc->heap_end = proc->heap_start;
    }

    if (new_brk == 0 || new_brk == proc->heap_end) {
        return proc->heap_end;
    }

    if (new_brk < proc->heap_start) {
        return proc->heap_end;
    }

    uint64_t current_page_end = PAGE_ALIGN_UP(proc->heap_end);
    uint64_t new_page_end = PAGE_ALIGN_UP(new_brk);

    if (new_page_end > current_page_end) {
        for (uint64_t page = current_page_end; page < new_page_end; page += PAGE_SIZE) {
            void *phys = pmm_alloc_page();
            if (!phys) return proc->heap_end;
            vmm_map_page(proc->pml4, page, (uint64_t)phys, VMM_USER_FLAGS);
        }
    } else if (new_page_end < current_page_end) {
        for (uint64_t page = new_page_end; page < current_page_end; page += PAGE_SIZE) {
            vmm_unmap_page(proc->pml4, page);
        }
    }

    proc->heap_end = new_brk;
    return proc->heap_end;
}

void syscall_handler(uint64_t *regs) {
    kprint("syscall\n", 0x00FFFF00);
    uint64_t num  = regs[14];
    uint64_t arg1 = regs[9];
    uint64_t arg2 = regs[10];
    uint64_t arg3 = regs[11];

    process_t *proc = get_current_process();

    switch (num) {
        case 0: {
            // Linux, REDHAT, IBM, gcc and exploit ss++
            int fd = (int)arg1;
            char *buf = (char *)arg2;
            size_t count = (size_t)arg3;

            if (fd == 0) {
                if (buf) {
                    buf[0] = kgetc();
                    regs[14] = 1;
                } else {
                    regs[14] = (uint64_t)-1;
                }
            } else if (fd >= 3 && fd < 16 && proc && proc->ofiles[fd].ip) {
                struct inode *ip = (struct inode *)proc->ofiles[fd].ip;
                ilock(ip);
                int r = readi(ip, buf, proc->ofiles[fd].offset, count);
                iunlock(ip);
                
                if (r > 0) {
                    proc->ofiles[fd].offset += r;
                }
                regs[14] = (uint64_t)r;
            } else {
                regs[14] = (uint64_t)-1;
            }
            break;
        }

        case 1: {
            int fd = (int)arg1;
            const char *buf = (const char *)arg2;
            size_t count = (size_t)arg3;

            if (fd == 1 || fd == 2) {
                if (buf) {
                    kprint(buf, 0x0000FF00);
                    regs[14] = count;
                } else {
                    regs[14] = (uint64_t)-1;
                }
            } else if (fd >= 3 && fd < 16 && proc && proc->ofiles[fd].ip) {
                struct inode *ip = (struct inode *)proc->ofiles[fd].ip;
                ilock(ip);
                int r = writei(ip, (char *)buf, proc->ofiles[fd].offset, count);
                iunlock(ip);
                
                if (r > 0) {
                    proc->ofiles[fd].offset += r;
                    iupdate(ip);
                }
                regs[14] = (uint64_t)r;
            } else {
                regs[14] = (uint64_t)-1;
            }
            break;
        }

        case 2: {
            const char *path = (const char *)arg1;
            int flags = (int)arg2;
            (void)arg3;

            if (!path || !proc) {
                regs[14] = (uint64_t)-1;
                break;
            }

            struct inode *ip = namei((char *)path);
            if (!ip) {
                regs[14] = (uint64_t)-1;
                break;
            }
            int fd = -1;
            for (int i = 3; i < 16; i++) {
                if (proc->ofiles[i].ip == 0) {
                    fd = i;
                    break;
                }
            }

            if (fd != -1) {
                proc->ofiles[fd].ip = ip;
                proc->ofiles[fd].offset = 0;
                proc->ofiles[fd].flags = flags;
                regs[14] = (uint64_t)fd;
            } else {
                iput(ip);
                regs[14] = (uint64_t)-1;
            }
            break;
        }

        case 3: {
            int fd = (int)arg1;
            
            if (fd >= 3 && fd < 16 && proc && proc->ofiles[fd].ip) {
                struct inode *ip = (struct inode *)proc->ofiles[fd].ip;
                iput(ip);
                
                proc->ofiles[fd].ip = 0;
                proc->ofiles[fd].offset = 0;
                proc->ofiles[fd].flags = 0;
                
                regs[14] = 0;
            } else {
                regs[14] = (uint64_t)-1;
            }
            break;
        }

        case 12:
            // Linux sys_brk
            regs[14] = sys_brk(arg1);
            break;

        case 59: {
            // Linux sys_execve
            const char *filename = (const char *)arg1;
            if (filename) {
                regs[14] = (uint64_t)spawn_from_ramfs(filename);
            } else {
                regs[14] = (uint64_t)-1;
            }
            break;
        }

        case 60:
            // Linux sys_exit
            kprint("[syscall] exit called\n", 0x00FF0000);
            exit_current_process();
            break;

        case 222: {
            // sys_blit
            const uint32_t *user_buf = (const uint32_t *)arg1;
            uint32_t width  = (uint32_t)arg2;
            uint32_t height = (uint32_t)arg3;

            if (user_buf && width <= 800 && height <= 600) {
                volatile uint64_t *fb = (volatile uint64_t *)(uintptr_t)vbeaddr();
                const uint64_t *src = (const uint64_t *)user_buf;

                if (fb) {
                    uint32_t total_pixels = width * height;
                    uint32_t count64 = total_pixels / 2;

                    for (uint32_t i = 0; i < count64; i++) {
                        fb[i] = src[i];
                    }

                    if (total_pixels % 2 != 0) {
                        ((volatile uint32_t*)fb)[total_pixels - 1] = ((const uint32_t*)src)[total_pixels - 1];
                    }
                }
            }
            break;
        }

        default:
            kprint("unknown syscall\n", 0x00FF00FF);
            regs[14] = (uint64_t)-1;
            break;
    }
}

void syscall_init(void) {
    extern void iset(int num, uint64_t base, uint16_t sel, uint8_t flags);
    extern void syscall_handler_stub(void);
    
    iset(0x80, (uint64_t)syscall_handler_stub, 0x08, 0xEE);
}