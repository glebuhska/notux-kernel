#include "stdint.h"

#define PAGE_SIZE 4096

#define MAX_PAGES (128 * 1024 * 1024 / PAGE_SIZE) 

static uint8_t bitmap[MAX_PAGES / 8];


static void bitmap_set(uint32_t page_index) {
    bitmap[page_index / 8] |= (1 << (page_index % 8));
}

static void bitmap_clear(uint32_t page_index) {
    bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

static int bitmap_test(uint32_t page_index) {
    return bitmap[page_index / 8] & (1 << (page_index % 8));
}

void pmm_init(uint32_t reserved_pages) {
    for (uint32_t i = 0; i < reserved_pages; i++) {
        bitmap_set(i);
    }
}

void* pmm_alloc_page(void) {
    for (uint32_t i = 0; i < MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return (void*)(uint64_t)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void* ptr) {
    uint32_t page_index = (uint64_t)ptr / PAGE_SIZE;
    bitmap_clear(page_index);
}