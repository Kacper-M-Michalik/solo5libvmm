#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <solo5libvmm/solo5/hvt_abi.h>

char* fault_to_string(seL4_Word fault_label);

bool fault_handle(size_t vcpu_id, microkit_msginfo msginfo, uint8_t* guest_mem, enum hvt_hypercall* hypercall_id, void** hypercall_data, seL4_UserContext* regs_at_fault);