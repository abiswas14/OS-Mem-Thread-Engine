#include "vm.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

static int vpn_from_addr(uint16_t vaddr) {
    return (int)(vaddr / VM_PAGE_SIZE) % VM_NUM_PAGES;
}

static int offset_from_addr(uint16_t vaddr) {
    return (int)(vaddr % VM_PAGE_SIZE);
}

static int allocate_swap_slot(VmSystem *vm) {
    for (int i = 0; i < VM_SWAP_PAGES; i++) {
        if (!vm->swap_used[i]) {
            vm->swap_used[i] = true;
            return i;
        }
    }
    return -1;
}

static int find_free_frame(VmSystem *vm) {
    for (int i = 0; i < VM_NUM_FRAMES; i++) {
        if (!vm->frame_table[i].protected_frame && !vm->frame_table[i].occupied) {
            return i;
        }
    }
    return -1;
}

static int select_fifo_victim(VmSystem *vm) {
    for (int step = 0; step < VM_NUM_FRAMES * 2; step++) {
        int candidate = (int)((vm->fifo_cursor + step) % VM_NUM_FRAMES);
        if (!vm->frame_table[candidate].protected_frame) {
            vm->fifo_cursor = (unsigned)((candidate + 1) % VM_NUM_FRAMES);
            return candidate;
        }
    }
    return -1;
}

static int select_lru_victim(VmSystem *vm) {
    int best = -1;
    unsigned best_age = UINT_MAX;
    for (int i = 0; i < VM_NUM_FRAMES; i++) {
        if (vm->frame_table[i].protected_frame) {
            continue;
        }
        if (!vm->frame_table[i].occupied) {
            return i;
        }
        if (vm->frame_table[i].age < best_age) {
            best_age = vm->frame_table[i].age;
            best = i;
        }
    }
    return best;
}

static int select_victim(VmSystem *vm) {
    int free_frame = find_free_frame(vm);
    if (free_frame >= 0) {
        return free_frame;
    }
    return vm->policy == VM_POLICY_FIFO ? select_fifo_victim(vm) : select_lru_victim(vm);
}

static void evict_if_needed(VmSystem *vm, int frame) {
    FrameEntry *fte = &vm->frame_table[frame];
    if (!fte->occupied) {
        return;
    }

    PageEntry *pte = &vm->page_table[fte->vpn];
    if (pte->dirty || fte->dirty) {
        int slot = pte->swap_slot;
        if (slot < 0) {
            slot = allocate_swap_slot(vm);
            pte->swap_slot = slot;
        }
        if (slot >= 0) {
            memcpy(vm->swap[slot], vm->memory[frame], VM_PAGE_SIZE);
            vm->stats.writebacks++;
        }
    }

    pte->valid = false;
    pte->dirty = false;
    pte->referenced = false;
    pte->frame = -1;

    memset(fte, 0, sizeof(*fte));
}

static int resolve_page(VmSystem *vm, int vpn) {
    PageEntry *pte = &vm->page_table[vpn];
    if (pte->valid) {
        return pte->frame;
    }

    vm->stats.page_faults++;
    int frame = select_victim(vm);
    if (frame < 0) {
        return -1;
    }

    evict_if_needed(vm, frame);

    if (pte->swap_slot >= 0 && vm->swap_used[pte->swap_slot]) {
        memcpy(vm->memory[frame], vm->swap[pte->swap_slot], VM_PAGE_SIZE);
    } else {
        memset(vm->memory[frame], 0, VM_PAGE_SIZE);
    }

    pte->valid = true;
    pte->dirty = false;
    pte->referenced = true;
    pte->frame = frame;

    vm->frame_table[frame].occupied = true;
    vm->frame_table[frame].protected_frame = false;
    vm->frame_table[frame].referenced = true;
    vm->frame_table[frame].dirty = false;
    vm->frame_table[frame].owner_pid = 1;
    vm->frame_table[frame].vpn = vpn;
    vm->frame_table[frame].age = 0x80;
    vm->frame_table[frame].loaded_at = vm->clock++;

    return frame;
}

void vm_init(VmSystem *vm, VmPolicy policy) {
    memset(vm, 0, sizeof(*vm));
    vm->policy = policy;
    vm->fifo_cursor = 1;

    for (int i = 0; i < VM_NUM_PAGES; i++) {
        vm->page_table[i].frame = -1;
        vm->page_table[i].swap_slot = -1;
    }

    /* Reserve frame 0 as kernel metadata, similar to a protected frame table. */
    vm->frame_table[0].occupied = true;
    vm->frame_table[0].protected_frame = true;
    vm->frame_table[0].owner_pid = 0;
    vm->frame_table[0].vpn = -1;
}

bool vm_write(VmSystem *vm, uint16_t vaddr, uint8_t value) {
    vm->stats.accesses++;
    int vpn = vpn_from_addr(vaddr);
    int offset = offset_from_addr(vaddr);
    int frame = resolve_page(vm, vpn);
    if (frame < 0) {
        return false;
    }

    vm->memory[frame][offset] = value;
    vm->page_table[vpn].dirty = true;
    vm->page_table[vpn].referenced = true;
    vm->frame_table[frame].dirty = true;
    vm->frame_table[frame].referenced = true;
    return true;
}

bool vm_read(VmSystem *vm, uint16_t vaddr, uint8_t *out) {
    vm->stats.accesses++;
    int vpn = vpn_from_addr(vaddr);
    int offset = offset_from_addr(vaddr);
    int frame = resolve_page(vm, vpn);
    if (frame < 0) {
        return false;
    }

    *out = vm->memory[frame][offset];
    vm->page_table[vpn].referenced = true;
    vm->frame_table[frame].referenced = true;
    return true;
}

void vm_daemon_tick(VmSystem *vm) {
    for (int i = 0; i < VM_NUM_FRAMES; i++) {
        if (vm->frame_table[i].protected_frame || !vm->frame_table[i].occupied) {
            continue;
        }
        vm->frame_table[i].age >>= 1;
        if (vm->frame_table[i].referenced) {
            vm->frame_table[i].age |= 0x80;
        }
        vm->frame_table[i].referenced = false;
        int vpn = vm->frame_table[i].vpn;
        if (vpn >= 0) {
            vm->page_table[vpn].referenced = false;
        }
    }
}

VmStats vm_compute_stats(VmSystem *vm) {
    VmStats stats = vm->stats;
    if (stats.accesses == 0) {
        stats.amat = 0.0;
        return stats;
    }

    double fault_rate = (double)stats.page_faults / (double)stats.accesses;
    double writeback_rate = (double)stats.writebacks / (double)stats.accesses;
    stats.amat = VM_READ_COST + fault_rate * VM_PAGE_FAULT_COST + writeback_rate * VM_SWAP_WRITE_COST;
    vm->stats.amat = stats.amat;
    return stats;
}

void vm_print_state(const VmSystem *vm) {
    printf("Frame table:\n");
    for (int i = 0; i < VM_NUM_FRAMES; i++) {
        const FrameEntry *f = &vm->frame_table[i];
        printf("  frame %d: occ=%d protected=%d vpn=%d dirty=%d age=%u\n",
               i, f->occupied, f->protected_frame, f->vpn, f->dirty, f->age);
    }
}

void vm_run_demo(void) {
    VmSystem vm;
    vm_init(&vm, VM_POLICY_APPROX_LRU);

    for (uint16_t i = 0; i < 40; i++) {
        uint16_t addr = (uint16_t)((i * 37) % (VM_PAGE_SIZE * VM_NUM_PAGES));
        vm_write(&vm, addr, (uint8_t)(i + 10));
        if (i % 4 == 0) {
            vm_daemon_tick(&vm);
        }
    }

    for (uint16_t i = 0; i < 24; i++) {
        uint8_t value = 0;
        uint16_t addr = (uint16_t)((i * 53) % (VM_PAGE_SIZE * VM_NUM_PAGES));
        vm_read(&vm, addr, &value);
        if (i % 5 == 0) {
            vm_daemon_tick(&vm);
        }
    }

    VmStats stats = vm_compute_stats(&vm);
    vm_print_state(&vm);
    printf("VM stats: accesses=%llu page_faults=%llu writebacks=%llu AMAT=%.2f\n",
           (unsigned long long)stats.accesses,
           (unsigned long long)stats.page_faults,
           (unsigned long long)stats.writebacks,
           stats.amat);
}
