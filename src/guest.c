#include <string.h>
#include <microkit.h>
#include <solo5libvmm/vcpu.h>
#include <solo5libvmm/guest.h>
#include <solo5libvmm/util/util.h>

//Change to resume?
void guest_resume(size_t boot_vcpu_id) 
{    
    LOG_VMM("Starting guest.");
    
    //Get usercontext here, and just run it

    microkit_vcpu_restart(boot_vcpu_id, kernel_pc);
}

void guest_stop(size_t boot_vcpu_id) {
    LOG_VMM("Stopping guest\n");
    microkit_vcpu_stop(boot_vcpu_id);
    LOG_VMM("Stopped guest\n");
}

bool guest_reset(size_t boot_vcpu_id) {
    LOG_VMM("Attempting to restart guest\n");
    microkit_vcpu_stop(boot_vcpu_id);
    LOG_VMM("Stopped guest\n");

    LOG_VMM("Clearing guest RAM\n");
    memset((char *)guest_ram_vaddr, 0, guest_ram_size);

    LOG_VMM("Resetting guest registers\n");
    vcpu_reset(boot_vcpu_id);    

    LOG_VMM("Guest reset\n");
    return true;
}