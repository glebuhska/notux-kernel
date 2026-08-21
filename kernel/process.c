#include "process.h"
#include <stdint.h>

process_t processes[MAX_PROCESSES];
int current_process_idx = 0;
static uint64_t next_pid = 1;

process_t* get_current_process(void) {
    return &processes[current_process_idx];
}

int create_process(uint64_t entry_point) {
    int idx = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_STATE_TERMINATED) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return -1;

    processes[idx].pid = next_pid++;
    
    static uint8_t kstacks[MAX_PROCESSES][4096];
    static uint8_t ustacks[MAX_PROCESSES][4096];

    uint64_t kernel_stack_top = (uint64_t)&kstacks[idx][4096];
    uint64_t user_stack_top = (uint64_t)&ustacks[idx][4096];

    processes[idx].kernel_stack = kernel_stack_top;

    uint64_t *stack = (uint64_t *)kernel_stack_top;

    *(--stack) = 0x1B;          // SS: User Data (0x18 | RPL 3)
    *(--stack) = user_stack_top;// RSP
    *(--stack) = 0x202;         // RFLAGS (IF = 1)
    *(--stack) = 0x23;          // CS: User Code (0x20 | RPL 3)
    *(--stack) = entry_point;   // RIP

    for (int i = 0; i < 15; i++) {
        *(--stack) = 0; 
    }

    processes[idx].esp = (uint64_t)stack;
    processes[idx].state = PROCESS_STATE_READY;

    return processes[idx].pid;
}

void exit_current_process(void) {
    processes[current_process_idx].state = PROCESS_STATE_TERMINATED;
    asm volatile ("sti");
    for (;;) {
        asm volatile ("hlt");
    }
}