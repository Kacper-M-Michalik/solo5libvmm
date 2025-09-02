#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <microkit.h>

/* Fault-handling functions */
bool fault_handle(size_t vcpu_id, microkit_msginfo msginfo);

bool fault_handle_vcpu_exception(size_t vcpu_id);
bool fault_handle_vppi_event(size_t vcpu_id);
bool fault_handle_user_exception(size_t vcpu_id);
bool fault_handle_unknown_syscall(size_t vcpu_id);
bool fault_handle_vm_exception(size_t vcpu_id);

char *fault_to_string(seL4_Word fault_label);

bool fault_is_write(uint64_t fsr);
bool fault_is_read(uint64_t fsr);