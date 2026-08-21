#ifndef PMM_H
#define PMM_H

#include "stdint.h"

void pmm_init(uint32_t reserved_pages);
void* pmm_alloc_page(void);
void pmm_free_page(void* ptr);

#endif