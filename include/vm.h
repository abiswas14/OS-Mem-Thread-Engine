#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define VM_PAGE_SIZE 64
#define VM_NUM_FRAMES 8
#define VM_NUM_PAGES 16
#define VM_SWAP_PAGES 16

#define VM_READ_COST 1.0
#define VM_PAGE_FAULT_COST 80.0
#define VM_SWAP_WRITE_COST 120.0

typedef enum {
    VM_POLICY_FIFO = 0,
    VM_POLICY_APPROX_LRU = 1
} VmPolicy;

typedef struct {
    bool valid;
    bool dirty;
    bool referenced;
    int frame;
    int swap_slot;
} PageEntry;

typedef struct {
    bool occupied;
    bool protected_frame;
    bool referenced;
    bool dirty;
    int owner_pid;
    int vpn;
    unsigned age;
    unsigned loaded_at;
} FrameEntry;

typedef struct {
    uint64_t accesses;
    uint64_t page_faults;
    uint64_t writebacks;
    double amat;
} VmStats;

typedef struct {
    VmPolicy policy;
    uint8_t memory[VM_NUM_FRAMES][VM_PAGE_SIZE];
    uint8_t swap[VM_SWAP_PAGES][VM_PAGE_SIZE];
    bool swap_used[VM_SWAP_PAGES];
    PageEntry page_table[VM_NUM_PAGES];
    FrameEntry frame_table[VM_NUM_FRAMES];
    unsigned clock;
    unsigned fifo_cursor;
    VmStats stats;
} VmSystem;

void vm_init(VmSystem *vm, VmPolicy policy);
bool vm_write(VmSystem *vm, uint16_t vaddr, uint8_t value);
bool vm_read(VmSystem *vm, uint16_t vaddr, uint8_t *out);
void vm_daemon_tick(VmSystem *vm);
VmStats vm_compute_stats(VmSystem *vm);
void vm_print_state(const VmSystem *vm);
void vm_run_demo(void);

#endif
