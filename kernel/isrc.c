#include "types.h"
#include "vga.h"
#include "stdint.h"
#include "serial.h"

extern void piceoi(unsigned char irq);

void timer_interrupt_handler(void) {
    serwrite(".");
    piceoi(0);
}

void isr_handler(uint64_t *rsp) {
    print("ISR triggered", 12, 0x4F);
}