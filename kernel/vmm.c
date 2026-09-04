#include "vmm.h"
#include "pmm.h"
#include "stdint.h"

#define PML4_IDX(addr) (((addr) >> 39) & 0x1FF)
#define PDP_IDX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_IDX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_IDX(addr)   (((addr) >> 12) & 0x1FF)
static uint64_t *kernel_pml4 = 0;


/* ============================================================
 * ZERO TABLE
 * ============================================================ */

static void zero_table(uint64_t *table)
{
    if (!table)
        return;

    for (int i = 0; i < 512; i++)
        table[i] = 0;
}


/* ============================================================
 * GET NEXT LEVEL
 *
 * Used for PML4 -> PDP and PDP -> PD.
 * ============================================================ */

static uint64_t *get_next_level(
    uint64_t *entry,
    int allocate
)
{
    if (!entry)
        return 0;


    /*
     * Existing normal page-table pointer.
     */
    if (*entry & PAGE_PRESENT)
    {
        /*
         * This helper must not be used for a huge PDE.
         */
        if (*entry & PAGE_HUGE)
            return 0;

        return (uint64_t *)(*entry & ~0xFFFULL);
    }


    if (!allocate)
        return 0;


    uint64_t new_table =
        (uint64_t)pmm_alloc_page();


    if (!new_table)
        return 0;


    uint64_t *ptr =
        (uint64_t *)new_table;


    zero_table(ptr);


    *entry =
        new_table |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;


    return ptr;
}


/* ============================================================
 * SPLIT 2 MiB HUGE PAGE INTO 4 KiB PAGES
 *
 * The old paging.c maps memory using 2 MiB pages.
 *
 * When a user ELF wants to replace one 4 KiB part of such
 * a huge page, create a private PT containing the old identity
 * mapping and replace the huge PDE with the PT.
 * ============================================================ */

static uint64_t *split_huge_pde(
    uint64_t *pde
)
{
    if (!pde)
        return 0;


    if (!(*pde & PAGE_PRESENT))
        return 0;


    if (!(*pde & PAGE_HUGE))
    {
        return (uint64_t *)
            (*pde & ~0xFFFULL);
    }


    /*
     * Physical base of the original 2 MiB page.
     */
    uint64_t huge_phys =
        *pde & ~0x1FFFFFULL;


    uint64_t old_flags =
        *pde &
        (PAGE_PRESENT |
         PAGE_WRITE |
         PAGE_USER);


    /*
     * Allocate a new page table.
     */
    uint64_t pt_phys =
        (uint64_t)pmm_alloc_page();


    if (!pt_phys)
        return 0;


    uint64_t *pt =
        (uint64_t *)pt_phys;


    /*
     * Convert the 2 MiB identity mapping into:
     *
     * 512 × 4 KiB pages.
     */
    for (uint64_t i = 0;
         i < 512;
         i++)
    {
        uint64_t phys =
            huge_phys +
            i * PAGE_SIZE;


        pt[i] =
            phys |
            old_flags;
    }


    /*
     * Replace huge PDE with PT pointer.
     */
    *pde =
        pt_phys |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;


    return pt;
}


/* ============================================================
 * VMM INIT
 * ============================================================ */

void vmm_init(void)
{
    uint64_t cr3;


    asm volatile(
        "mov %%cr3, %0"
        : "=r"(cr3)
    );


    kernel_pml4 =
        (uint64_t *)(cr3 & ~0xFFFULL);
}


/* ============================================================
 * GET KERNEL PML4
 * ============================================================ */

uint64_t *vmm_get_kernel_pml4(void)
{
    return kernel_pml4;
}


/* ============================================================
 * MAP ONE 4 KiB PAGE
 * ============================================================ */

void vmm_map_page(
    uint64_t *pml4,
    uint64_t virt,
    uint64_t phys,
    uint64_t flags
)
{
    if (!pml4)
        return;


    /*
     * --------------------------------------------------------
     * PML4 -> PDP
     * --------------------------------------------------------
     */

    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            1
        );


    if (!pdp)
        return;


    /*
     * --------------------------------------------------------
     * PDP -> PD
     * --------------------------------------------------------
     */

    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            1
        );


    if (!pd)
        return;


    /*
     * --------------------------------------------------------
     * PD -> PT
     *
     * This is where the old paging system has a 2 MiB
     * PAGE_HUGE entry.
     * --------------------------------------------------------
     */

    uint64_t *pde =
        &pd[PD_IDX(virt)];


    uint64_t *pt;


    if (*pde & PAGE_PRESENT)
    {
        /*
         * Huge page:
         * split it first.
         */
        if (*pde & PAGE_HUGE)
        {
            pt =
                split_huge_pde(
                    pde
                );
        }
        else
        {
            pt =
                (uint64_t *)
                (*pde & ~0xFFFULL);
        }
    }
    else
    {
        /*
         * No PT yet.
         */
        uint64_t new_pt =
            (uint64_t)pmm_alloc_page();


        if (!new_pt)
            return;


        pt =
            (uint64_t *)new_pt;


        zero_table(pt);


        *pde =
            new_pt |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_USER;
    }


    if (!pt)
        return;


    /*
     * --------------------------------------------------------
     * PTE
     * --------------------------------------------------------
     */

    pt[PT_IDX(virt)] =
        (phys & ~0xFFFULL) |
        flags |
        PAGE_PRESENT;


    /*
     * --------------------------------------------------------
     * Flush TLB.
     * --------------------------------------------------------
     */

    asm volatile(
        "invlpg (%0)"
        :
        : "r"(virt)
        : "memory"
    );
}


/* ============================================================
 * UNMAP ONE 4 KiB PAGE
 * ============================================================ */

void vmm_unmap_page(
    uint64_t *pml4,
    uint64_t virt
)
{
    if (!pml4)
        return;


    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            0
        );


    if (!pdp)
        return;


    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            0
        );


    if (!pd)
        return;


    uint64_t *pde =
        &pd[PD_IDX(virt)];


    /*
     * We cannot unmap a single 4 KiB page from a huge page
     * without splitting it first.
     */
    uint64_t *pt;


    if (*pde & PAGE_HUGE)
    {
        pt =
            split_huge_pde(
                pde
            );
    }
    else
    {
        if (!(*pde & PAGE_PRESENT))
            return;


        pt =
            (uint64_t *)
            (*pde & ~0xFFFULL);
    }


    if (!pt)
        return;


    pt[PT_IDX(virt)] = 0;


    asm volatile(
        "invlpg (%0)"
        :
        : "r"(virt)
        : "memory"
    );
}


/* ============================================================
 * VIRTUAL -> PHYSICAL
 * ============================================================ */

uint64_t vmm_virt_to_phys(
    uint64_t *pml4,
    uint64_t virt
)
{
    if (!pml4)
        return 0;


    uint64_t *pdp =
        get_next_level(
            &pml4[PML4_IDX(virt)],
            0
        );


    if (!pdp)
        return 0;


    uint64_t *pd =
        get_next_level(
            &pdp[PDP_IDX(virt)],
            0
        );


    if (!pd)
        return 0;


    uint64_t pde =
        pd[PD_IDX(virt)];


    if (!(pde & PAGE_PRESENT))
        return 0;


    /*
     * Handle 2 MiB huge page.
     */
    if (pde & PAGE_HUGE)
    {
        uint64_t base =
            pde & ~0x1FFFFFULL;


        return base |
            (virt & 0x1FFFFFULL);
    }


    uint64_t *pt =
        (uint64_t *)
        (pde & ~0xFFFULL);


    if (!pt)
        return 0;


    uint64_t pte =
        pt[PT_IDX(virt)];


    if (!(pte & PAGE_PRESENT))
        return 0;


    return
        (pte & ~0xFFFULL) |
        (virt & 0xFFFULL);
}


/* ============================================================
 * CREATE USER PML4
 * ============================================================ */

uint64_t *create_user_pml4(void)
{
    /*
     * --------------------------------------------------------
     * New PML4
     * --------------------------------------------------------
     */

    uint64_t *user_pml4 =
        (uint64_t *)pmm_alloc_page();


    if (!user_pml4)
        return 0;


    zero_table(
        user_pml4
    );


    /*
     * --------------------------------------------------------
     * Kernel PML4
     * --------------------------------------------------------
     */

    uint64_t *k_pml4 =
        vmm_get_kernel_pml4();


    if (!k_pml4)
        return 0;


    /*
     * --------------------------------------------------------
     * Clone PML4[0] hierarchy.
     *
     * Do NOT share kernel PD.
     *
     * The PDE values themselves may remain huge pages.
     * When ELF mapping touches a huge PDE, vmm_map_page()
     * will split the private PDE into a private PT.
     * --------------------------------------------------------
     */

    uint64_t k_pml4e =
        k_pml4[0];


    if (!(k_pml4e & PAGE_PRESENT))
        return user_pml4;


    uint64_t *k_pdp =
        (uint64_t *)
        (k_pml4e & ~0xFFFULL);


    if (!k_pdp)
        return user_pml4;


    /*
     * New PDP.
     */
    uint64_t *user_pdp =
        (uint64_t *)pmm_alloc_page();


    if (!user_pdp)
        return 0;


    zero_table(
        user_pdp
    );


    user_pml4[0] =
        ((uint64_t)user_pdp) |
        PAGE_PRESENT |
        PAGE_WRITE |
        PAGE_USER;


    /*
     * Clone the low identity map.
     *
     * boot.asm sets up FOUR PDPT entries (pdpt_table + 0/8/16/24),
     * i.e. a full 4 GiB identity map via 2 MiB pages, not just the
     * first 1 GiB. Anything living above 1 GiB physical/virtual
     * (e.g. the VBE/PCI framebuffer BAR, typically mapped far above
     * regular RAM) was previously left unmapped in every spawned
     * process's page tables, since only k_pdp[0] was cloned here.
     *
     * That's harmless as long as CR3 is never switched away from
     * the kernel's own boot-time tables, but becomes a page fault
     * (not-present) the moment a process actually runs under its
     * own private PML4 and touches anything past the 1 GiB mark.
     */
    for (int pdpt_idx = 0;
         pdpt_idx < 4;
         pdpt_idx++)
    {
        if (!(k_pdp[pdpt_idx] & PAGE_PRESENT))
            continue;


        uint64_t *k_pd_n =
            (uint64_t *)
            (k_pdp[pdpt_idx] & ~0xFFFULL);


        if (!k_pd_n)
            continue;


        uint64_t *user_pd_n =
            (uint64_t *)pmm_alloc_page();


        if (!user_pd_n)
            return 0;


        zero_table(
            user_pd_n
        );


        user_pdp[pdpt_idx] =
            ((uint64_t)user_pd_n) |
            PAGE_PRESENT |
            PAGE_WRITE |
            PAGE_USER;


        /*
         * Copy PDEs only (huge-page values, not shared PD).
         */
        for (int i = 0;
             i < 512;
             i++)
        {
            user_pd_n[i] =
                k_pd_n[i];
        }
    }


    /*
     * Copy remaining PML4 entries.
     *
     * Current kernel mostly uses PML4[0].
     * Keeping the rest gives us a reasonable kernel
     * address-space compatibility.
     */
    for (int i = 1;
         i < 512;
         i++)
    {
        user_pml4[i] =
            k_pml4[i];
    }


    return user_pml4;
}


/* ============================================================
 * SWITCH ADDRESS SPACE
 * ============================================================ */

void vmm_switch_pml4(
    uint64_t *pml4
)
{
    if (!pml4)
        return;


    asm volatile(
        "mov %0, %%cr3"
        :
        : "r"((uint64_t)pml4)
        : "memory"
    );
}