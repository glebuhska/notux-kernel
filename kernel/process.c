#include "process.h"
#include "stdint.h"
#include "vmm.h"
#include "serial.h"
#include "pmm.h"


/* ============================================================
 * GLOBAL PROCESS TABLE
 * ============================================================ */

process_t processes[MAX_PROCESSES];

int current_process_idx = 0;

static uint64_t next_pid = 1;


/* ============================================================
 * PROCESS STACKS
 * ============================================================ */

/*
 * Kernel stack for every process.
 */
static uint8_t kernel_stacks[MAX_PROCESSES][4096]
    __attribute__((aligned(16)));


/*
 * Temporary user stack for every process.
 *
 * Сейчас используется через identity mapping.
 * Потом можно заменить на полноценную user-space mapping.
 */
static uint8_t user_stacks[MAX_PROCESSES][4096]
    __attribute__((aligned(16)));


extern void set_kernel_stack(uint64_t stack);


/* ============================================================
 * scheduler_init
 * ============================================================ */

void scheduler_init(void)
{
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        processes[i].pid = 0;

        processes[i].esp = 0;

        processes[i].kernel_stack = 0;

        processes[i].state =
            PROCESS_STATE_TERMINATED;

        processes[i].pml4 = 0;

        processes[i].heap_start = 0;

        processes[i].heap_end = 0;


        for (int j = 0;
             j < 16;
             j++)
        {
            processes[i].ofiles[j].ip = 0;

            processes[i].ofiles[j].offset = 0;

            processes[i].ofiles[j].flags = 0;
        }
    }


    current_process_idx = 0;

    next_pid = 1;


    serwrite(
        "SCHEDULER: initialized\n"
    );
}


/* ============================================================
 * get_current_process
 * ============================================================ */

process_t *get_current_process(void)
{
    return &processes[
        current_process_idx
    ];
}


/* ============================================================
 * get_process_by_pid
 * ============================================================ */

process_t *get_process_by_pid(
    uint32_t pid
)
{
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        if (processes[i].pid == pid &&
            processes[i].state !=
                PROCESS_STATE_TERMINATED)
        {
            return &processes[i];
        }
    }


    return 0;
}


/* ============================================================
 * create_process
 * ============================================================ */

int create_process(
    uint64_t entry_point
)
{
    int idx = -1;


    /*
     * Find free slot.
     */
    for (int i = 0;
         i < MAX_PROCESSES;
         i++)
    {
        if (processes[i].state ==
            PROCESS_STATE_TERMINATED)
        {
            idx = i;
            break;
        }
    }


    if (idx < 0)
    {
        serwrite(
            "PROCESS: no free slot\n"
        );

        return -1;
    }


    /*
     * --------------------------------------------------------
     * Create process page table.
     * --------------------------------------------------------
     */

    uint64_t *pml4 =
        create_user_pml4();


    if (!pml4)
    {
        serwrite(
            "PROCESS: create_user_pml4 failed\n"
        );

        return -1;
    }


    processes[idx].pml4 =
        pml4;


    /*
     * --------------------------------------------------------
     * Kernel stack.
     * --------------------------------------------------------
     */

    uint64_t kernel_stack_top =
        (uint64_t)
        &kernel_stacks[idx][
            sizeof(kernel_stacks[idx])
        ];


    /*
     * --------------------------------------------------------
     * User stack.
     *
     * Пока это обычная kernel static memory,
     * доступная через identity mapping.
     * --------------------------------------------------------
     */

    uint64_t user_stack_top =
        (uint64_t)
        &user_stacks[idx][
            sizeof(user_stacks[idx])
        ];


    processes[idx].kernel_stack =
        kernel_stack_top;


    /*
     * --------------------------------------------------------
     * Build initial context.
     * --------------------------------------------------------
     *
     * timer.asm:
     *
     *   pop r15
     *   ...
     *   pop rax
     *   iretq
     *
     * Поэтому stack:
     *
     *   R15
     *   R14
     *   R13
     *   R12
     *   R11
     *   R10
     *   R9
     *   R8
     *   RBP
     *   RDI
     *   RSI
     *   RDX
     *   RCX
     *   RBX
     *   RAX
     *
     *   RIP
     *   CS
     *   RFLAGS
     *   RSP
     *   SS
     * --------------------------------------------------------
     */

    uint64_t *stack =
        (uint64_t *)kernel_stack_top;


    /*
     * iretq frame.
     */

    *(--stack) =
        0x1B;                  /* SS */

    *(--stack) =
        user_stack_top;        /* RSP */

    *(--stack) =
        0x202;                 /* RFLAGS */

    *(--stack) =
        0x23;                  /* CS */

    *(--stack) =
        entry_point;           /* RIP */


    /*
     * General-purpose registers.
     */

    for (int i = 0;
         i < 15;
         i++)
    {
        *(--stack) = 0;
    }


    /*
     * Saved ESP.
     */

    processes[idx].esp =
        (uint64_t)stack;


    /*
     * PID.
     */

    processes[idx].pid =
        (uint32_t)next_pid++;


    /*
     * Heap.
     */

    processes[idx].heap_start = 0;

    processes[idx].heap_end = 0;


    /*
     * File descriptors.
     */

    for (int i = 0;
         i < 16;
         i++)
    {
        processes[idx].ofiles[i].ip = 0;

        processes[idx].ofiles[i].offset = 0;

        processes[idx].ofiles[i].flags = 0;
    }


    /*
     * Ready state.
     */

    processes[idx].state =
        PROCESS_STATE_READY;


    serwrite(
        "PROCESS: created\n"
    );


    return processes[idx].pid;
}


/* ============================================================
 * set_process_entry
 * ============================================================ */

void set_process_entry(
    process_t *proc,
    uint64_t entry_point
)
{
    if (!proc)
        return;


    if (!proc->esp)
        return;


    /*
     * Current ESP points at R15.
     *
     * Therefore:
     *
     *   0..14 = saved registers
     *   15    = RIP
     *   16    = CS
     *   17    = RFLAGS
     *   18    = RSP
     *   19    = SS
     */

    uint64_t *stack =
        (uint64_t *)proc->esp;


    stack[15] =
        entry_point;
}


/* ============================================================
 * activate_process
 * ============================================================ */

static void activate_process(
    int idx
)
{
    if (idx < 0)
        return;


    if (idx >= MAX_PROCESSES)
        return;


    current_process_idx =
        idx;


    processes[idx].state =
        PROCESS_STATE_RUNNING;


    /*
     * Update TSS/RSP0.
     */
    set_kernel_stack(
        processes[idx].kernel_stack
    );


    /*
     * Switch to this process's own address space.
     *
     * Without this, every process runs under whatever CR3
     * happened to be active last, so any process besides the
     * very first one executes against the wrong page tables
     * (page fault / garbage execution as soon as it touches
     * memory outside the shared identity-mapped region).
     */
    if (processes[idx].pml4)
    {
        vmm_switch_pml4(
            processes[idx].pml4
        );
    }
}


/* ============================================================
 * schedule
 * ============================================================ */

uint64_t schedule(
    uint64_t current_esp
)
{
    serwrite(
        "SCHEDULE: enter\n"
    );


    process_t *current =
        &processes[
            current_process_idx
        ];


    /*
     * --------------------------------------------------------
     * Save current process context.
     * --------------------------------------------------------
     */

    if (current->state ==
        PROCESS_STATE_RUNNING)
    {
        current->esp =
            current_esp;
    }


    /*
     * --------------------------------------------------------
     * Search for another READY process.
     * --------------------------------------------------------
     */

    int next_idx = -1;


    for (int i = 1;
         i < MAX_PROCESSES;
         i++)
    {
        int idx =
            (current_process_idx + i)
            % MAX_PROCESSES;


        if (processes[idx].state ==
            PROCESS_STATE_READY)
        {
            next_idx =
                idx;

            break;
        }
    }


    /*
     * --------------------------------------------------------
     * Another process exists.
     * --------------------------------------------------------
     */

    if (next_idx >= 0)
    {
        /*
         * Only requeue the outgoing process if it's still
         * actually alive.
         *
         * Previously this unconditionally reset `current->state`
         * to READY, which "resurrected" processes that had just
         * been marked TERMINATED (e.g. by exit_current_process(),
         * which sets TERMINATED and then idles in a ring-0 hlt
         * loop without ever switching current_process_idx away).
         * The very next timer tick would then see that same slot,
         * force it back to READY, and later resume it from its
         * last saved (stale) esp -- replaying its entire
         * execution from that point, over and over.
         */
        if (current->state ==
            PROCESS_STATE_RUNNING)
        {
            current->state =
                PROCESS_STATE_READY;
        }


        activate_process(
            next_idx
        );


        serwrite(
            "SCHEDULE: switched\n"
        );


        return processes[
            current_process_idx
        ].esp;
    }


    /*
     * --------------------------------------------------------
     * No other process exists.
     *
     * Continue current process.
     * --------------------------------------------------------
     */

    if (current->state !=
        PROCESS_STATE_TERMINATED)
    {
        current->state =
            PROCESS_STATE_RUNNING;


        return current->esp;
    }


    /*
     * --------------------------------------------------------
     * No runnable processes.
     * --------------------------------------------------------
     */

    serwrite(
        "SCHEDULE: no runnable process\n"
    );


    asm volatile("sti");


    for (;;)
    {
        asm volatile("hlt");
    }
}


/* ============================================================
 * exit_current_process
 * ============================================================ */

void exit_current_process(void)
{
    /*
     * Mark current process as terminated.
     */
    processes[
        current_process_idx
    ].state =
        PROCESS_STATE_TERMINATED;


    serwrite(
        "PROCESS: terminated\n"
    );


    /*
     * At the moment the process waits for the next timer IRQ.
     *
     * The scheduler will see TERMINATED and choose another
     * READY process.
     */
    asm volatile("sti");


    for (;;)
    {
        asm volatile("hlt");
    }
}
