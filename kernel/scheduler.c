#include "process.h"
#include <stdint.h>

extern void set_kernel_stack(uint64_t stack);

void scheduler_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].state = PROCESS_STATE_TERMINATED;
    }
}

uint64_t schedule(uint64_t current_esp) {
    if (processes[current_process_idx].state == PROCESS_STATE_RUNNING) {
        processes[current_process_idx].esp = current_esp;
        processes[current_process_idx].state = PROCESS_STATE_READY;
    }

    int next_idx = current_process_idx;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        next_idx = (next_idx + 1) % MAX_PROCESSES;
        if (processes[next_idx].state == PROCESS_STATE_READY) {
            break;
        }
    }

    current_process_idx = next_idx;
    processes[current_process_idx].state = PROCESS_STATE_RUNNING;

    set_kernel_stack(processes[current_process_idx].kernel_stack);

    return processes[current_process_idx].esp;
}