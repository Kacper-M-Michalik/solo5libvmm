#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

//Executes from current PC value, need to figure out if pc points to next or last executed
bool guest_resume(size_t boot_vcpu_id);
//Just pause guest, gonna need to figure out how pc is setup
void guest_stop(size_t boot_vcpu_id);
//Clears guest registers and memory, allows for setting up new guest image after 
bool guest_reset(size_t boot_vcpu_id);