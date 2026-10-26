#pragma once
#include "kernel.h"

struct limine_memmap_response;

void     pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);
uint64_t pmm_alloc(void);              /* physical address of a 4 KiB page, 0 on OOM */
uint64_t pmm_alloc_zeroed(void);
void     pmm_free(uint64_t phys);
uint64_t pmm_alloc_contiguous(uint64_t pages);   /* physically contiguous run, 0 on failure */
uint64_t pmm_total_bytes(void);        /* usable RAM managed by the PMM */
uint64_t pmm_free_bytes(void);

extern uint64_t hhdm_offset;
static inline void *phys_to_virt(uint64_t phys) { return (void *)(phys + hhdm_offset); }
