#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Should add length of command line and mft?
/*  Sets up memory and VCpu registers of virtual guest
    boot_vcpu_id - ID of VCpu for guest to run on, should always be 0 as a VMM only runs 1 guest, but left as param for future changes
    kernel - Pointer to guest image
    kernel_size - Size of guest image in bytes
    virtual_memory_offset - VMM can map in guest memory at a different address than what guest sees (from guests perspective memory always starts from address 0), this is the difference in bytes
    memory_size - Total memory given to guest, this includes space for guest image, stack and heap, should be page (4k) aligned, error if not
    max_stack_size - Maximum stack size for guest, if exceeds available memory after image load it will cause error, if set to 0 no limit is set, uses 4k of memory due to setup of protection page
    cmdline - Command line arg passed to guest as per HVT spec
    mft - Mft structure passed to guest, represents available devices as per HVT spec
*/
void solo5_setup_guest(size_t boot_vcpu_id, uintptr_t kernel, size_t kernel_size, uintptr_t virtual_memory_offset, size_t memory_size, size_t max_stack_size, const char* cmdline, const void* mft);