// ============================================================
// NeuralOS X — Memory Manager  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// Implements: init_memory(), page allocator, region tracker
// Follows instructor guidance: E820 map at 0x9000/0x9008,
// static FreeRegion list, static Page free_memory,
// static memory_end, static total_mem.
// ============================================================

#include "../include/kernel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <string>

// ──────────────────────────────────────────────────────────────
//  Static Memory State  (instructor-mandated variable names)
// ──────────────────────────────────────────────────────────────
static FreeRegion* free_region_list = nullptr;  // linked list of free E820 regions
static Page*       free_memory      = nullptr;  // linked list of free 4 KiB pages
static uint64_t    memory_end       = 0;        // highest usable physical address
static uint64_t    total_mem        = 0;        // total usable RAM in bytes

// ──────────────────────────────────────────────────────────────
//  Simulated Physical Memory Pool
//  (In a real kernel this is actual physical RAM.
//   In this simulator we allocate a flat buffer and hand out
//   4 KiB-aligned slices from it so everything stays static.)
// ──────────────────────────────────────────────────────────────
static constexpr uint64_t SIM_RAM_BASE = 0x100000ULL;   // 1 MiB (skip low-mem)
static uint8_t*            sim_ram_pool = nullptr;        // flat backing store
static uint64_t            sim_ram_size = 0;              // configured via init

// ──────────────────────────────────────────────────────────────
//  Pool of pre-allocated region / page node objects
//  (avoids calling malloc after init — simulates a real kernel
//   slab allocator)
// ──────────────────────────────────────────────────────────────
static constexpr int MAX_REGIONS = 64;
static constexpr int MAX_PAGES   = 4096;  // 4096 pages × 4KiB = 16 MiB max addressable

static FreeRegion region_pool[MAX_REGIONS];
static Page       page_pool[MAX_PAGES];
static int        region_pool_idx = 0;
static int        page_pool_idx   = 0;

static FreeRegion* alloc_region_node() {
    if (region_pool_idx >= MAX_REGIONS) return nullptr;
    return &region_pool[region_pool_idx++];
}

static Page* alloc_page_node() {
    if (page_pool_idx >= MAX_PAGES) return nullptr;
    return &page_pool[page_pool_idx++];
}

// ──────────────────────────────────────────────────────────────
//  Simulated E820 Table
//  In hardware this lives at physical 0x9008; here we build it
//  in a static buffer and return a pointer — matching the
//  instructor's:  struct E820 *mem_map = (struct E820*)0x9008;
// ──────────────────────────────────────────────────────────────
static E820 simulated_e820[8];
static int  e820_entry_count = 0;

static void build_e820_table(uint64_t ram_bytes) {
    // Entry 0: Low RAM (0 – 640 KiB) — conventional usable
    simulated_e820[0] = { 0x00000000ULL, 0x000A0000ULL, 1, 1 };
    // Entry 1: BIOS / VGA hole (640 KiB – 1 MiB) — reserved
    simulated_e820[1] = { 0x000A0000ULL, 0x00060000ULL, 2, 1 };
    // Entry 2: Extended RAM starting at 1 MiB
    simulated_e820[2] = { SIM_RAM_BASE, ram_bytes, 1, 1 };
    // Entry 3: ACPI tables region (simulated just above RAM)
    simulated_e820[3] = { SIM_RAM_BASE + ram_bytes, 0x00010000ULL, 3, 1 };
    e820_entry_count  = 4;
}

// ──────────────────────────────────────────────────────────────
//  init_memory()  — as specified by instructor
// ──────────────────────────────────────────────────────────────
void init_memory(uint64_t configured_ram_bytes) {
    printf("[MEM] Initialising memory manager...\n");

    // ── Step 1: Build simulated E820 table ──────────────────────
    build_e820_table(configured_ram_bytes);

    // ── Step 2: Replicate instructor pattern ────────────────────
    //   int32_t memory   = *(int32_t *)0x9000;   <- number of E820 entries
    //   struct E820 *mem_map = (struct E820*)0x9008; <- pointer to table
    // In simulation we use our static arrays; the cast pattern is
    // kept identical so the code structure matches exactly.
    int32_t  memory  = e820_entry_count;          // count at "0x9000"
    E820*    mem_map = simulated_e820;             // table  at "0x9008"

    printf("[MEM] E820 map has %d entries:\n", memory);
    for (int i = 0; i < memory; i++) {
        printf("  [%d] base=0x%016llx  len=0x%016llx  type=%u\n",
               i,
               (unsigned long long)mem_map[i].base,
               (unsigned long long)mem_map[i].length,
               mem_map[i].type);
    }

    // ── Step 3: Allocate flat backing store ─────────────────────
    //  (static address — must not change after init, as instructor noted)
    sim_ram_size = configured_ram_bytes;
    sim_ram_pool = (uint8_t*)malloc(sim_ram_size);
    if (!sim_ram_pool) {
        fprintf(stderr, "[MEM] FATAL: Could not allocate simulator RAM pool\n");
        exit(1);
    }
    memset(sim_ram_pool, 0, sim_ram_size);

    // ── Step 4: Build FreeRegion list from usable E820 entries ──
    free_region_list = nullptr;
    FreeRegion* tail  = nullptr;

    for (int i = 0; i < memory; i++) {
        if (mem_map[i].type != 1) continue;  // skip non-usable

        // Only track the extended RAM region (skip low 640 KiB for simplicity)
        if (mem_map[i].base < SIM_RAM_BASE) continue;

        FreeRegion* rn = alloc_region_node();
        if (!rn) { fprintf(stderr, "[MEM] Region pool exhausted\n"); break; }
        rn->base   = mem_map[i].base;
        rn->length = mem_map[i].length;
        rn->next   = nullptr;

        if (!free_region_list) {
            free_region_list = rn;
            tail = rn;
        } else {
            tail->next = rn;
            tail = rn;
        }

        total_mem  += mem_map[i].length;
        uint64_t region_end = mem_map[i].base + mem_map[i].length;
        if (region_end > memory_end) memory_end = region_end;
    }

    printf("[MEM] Total usable RAM: %llu bytes (%.1f MiB)\n",
           (unsigned long long)total_mem,
           (double)total_mem / (1024.0 * 1024.0));
    printf("[MEM] memory_end = 0x%016llx\n", (unsigned long long)memory_end);

    // ── Step 5: Build free_memory page list ─────────────────────
    free_memory = nullptr;
    Page* page_tail = nullptr;
    uint64_t pages_created = 0;

    FreeRegion* rgn = free_region_list;
    while (rgn) {
        uint64_t addr  = rgn->base;
        uint64_t limit = rgn->base + rgn->length;
        // align start to page boundary
        addr = (addr + PAGE_SIZE_BYTES - 1) & ~(uint64_t)(PAGE_SIZE_BYTES - 1);

        while (addr + PAGE_SIZE_BYTES <= limit && page_pool_idx < MAX_PAGES) {
            Page* pg  = alloc_page_node();
            pg->address = addr;
            pg->in_use  = false;
            pg->next    = nullptr;

            if (!free_memory) {
                free_memory = pg;
                page_tail   = pg;
            } else {
                page_tail->next = pg;
                page_tail       = pg;
            }

            addr += PAGE_SIZE_BYTES;
            pages_created++;
        }
        rgn = rgn->next;
    }

    printf("[MEM] Page list built: %llu pages × %d bytes = %.1f MiB tracked\n",
           (unsigned long long)pages_created,
           PAGE_SIZE_BYTES,
           (double)(pages_created * PAGE_SIZE_BYTES) / (1024.0 * 1024.0));
    printf("[MEM] Memory manager ready.\n\n");
}

// ──────────────────────────────────────────────────────────────
//  alloc_pages(n)  — remove n pages from free_memory list,
//                    return pointer to first page node, or nullptr
// ──────────────────────────────────────────────────────────────
Page* alloc_pages(int n) {
    if (n <= 0 || !free_memory) return nullptr;

    Page* head = free_memory;
    Page* cur  = head;
    int   got  = 0;

    while (cur && got < n) {
        cur->in_use = true;
        got++;
        if (got < n) cur = cur->next;
    }

    if (got < n) {
        // not enough pages — roll back
        cur = head;
        for (int i = 0; i < got; i++) { cur->in_use = false; cur = cur->next; }
        return nullptr;
    }

    // Detach these pages from the free list
    free_memory = cur->next;
    cur->next   = nullptr;  // terminate the allocated chain
    return head;
}

// ──────────────────────────────────────────────────────────────
//  free_pages(head)  — return a page chain back to free_memory
// ──────────────────────────────────────────────────────────────
void free_pages(Page* head) {
    if (!head) return;
    Page* cur = head;
    while (cur->next) { cur->in_use = false; cur = cur->next; }
    cur->in_use = false;
    // Prepend chain back to free list
    cur->next   = free_memory;
    free_memory = head;
}

// ──────────────────────────────────────────────────────────────
//  mem_alloc_mb / mem_free_mb  — MB-level helpers for the kernel
//  resource manager (converts MiB request → page count)
// ──────────────────────────────────────────────────────────────
Page* mem_alloc_mb(int mb) {
    int pages_needed = (mb * 1024 * 1024 + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
    return alloc_pages(pages_needed);
}

void mem_free_mb(Page* head) {
    free_pages(head);
}

// ──────────────────────────────────────────────────────────────
//  Query helpers
// ──────────────────────────────────────────────────────────────
uint64_t mem_total_bytes()   { return total_mem; }
uint64_t mem_end_address()   { return memory_end; }

uint64_t mem_free_bytes() {
    uint64_t free_pages_count = 0;
    Page* p = free_memory;
    while (p) { free_pages_count++; p = p->next; }
    return free_pages_count * PAGE_SIZE_BYTES;
}

void mem_print_stats() {
    uint64_t used = total_mem - mem_free_bytes();
    printf("[MEM] Stats: total=%llu MiB  used=%llu MiB  free=%llu MiB\n",
           (unsigned long long)total_mem / (1024*1024),
           (unsigned long long)used / (1024*1024),
           (unsigned long long)mem_free_bytes() / (1024*1024));
}
