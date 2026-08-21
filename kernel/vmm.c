#include "vmm.h"
#include "pmm.h"

#define PML4_IDX(addr) (((addr) >> 39) & 0x1FF)
#define PDP_IDX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_IDX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_IDX(addr)   (((addr) >> 12) & 0x1FF)

static uint64_t *kernel_pml4 = 0;

static uint64_t* get_next_level(uint64_t *entry, int allocate) {
    if (*entry & PAGE_PRESENT) {
        return (uint64_t*)(*entry & ~0xFFFull);
    }
    
    if (!allocate) return 0;

    uint64_t new_table = (uint64_t)pmm_alloc_page();
    if (!new_table) return 0;

    uint64_t *ptr = (uint64_t*)new_table;
    for (int i = 0; i < 512; i++) {
        ptr[i] = 0;
    }

    *entry = new_table | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    return ptr;
}

void vmm_init(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    kernel_pml4 = (uint64_t*)(cr3 & ~0xFFFull);
}

uint64_t* vmm_get_kernel_pml4(void) {
    return kernel_pml4;
}

void vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pdp = get_next_level(&pml4[PML4_IDX(virt)], 1);
    uint64_t *pd  = get_next_level(&pdp[PDP_IDX(virt)], 1);
    uint64_t *pt  = get_next_level(&pd[PD_IDX(virt)], 1);

    pt[PT_IDX(virt)] = (phys & ~0xFFFull) | flags | PAGE_PRESENT;

    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void vmm_unmap_page(uint64_t *pml4, uint64_t virt) {
    uint64_t *pdp = get_next_level(&pml4[PML4_IDX(virt)], 0);
    if (!pdp) return;
    
    uint64_t *pd = get_next_level(&pdp[PDP_IDX(virt)], 0);
    if (!pd) return;

    uint64_t *pt = get_next_level(&pd[PD_IDX(virt)], 0);
    if (!pt) return;

    pt[PT_IDX(virt)] = 0;
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

uint64_t* create_user_pml4(void) {
    uint64_t *user_pml4 = (uint64_t*)pmm_alloc_page();
    if (!user_pml4) return 0;
    for (int i = 0; i < 256; i++) {
        user_pml4[i] = 0;
    }
    uint64_t *k_pml4 = vmm_get_kernel_pml4();
    for (int i = 256; i < 512; i++) {
        user_pml4[i] = k_pml4[i];
    }

    return user_pml4;
}