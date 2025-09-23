#include <microkit.h>
#include <solo5libvmm/aarch64/vcpu.h>
#include <solo5libvmm/solo5/hvt_abi.h>
#include <solo5libvmm/util.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

char* fault_to_string(seL4_Word fault_label)
{
    switch (fault_label)
    {
        case seL4_Fault_VMFault:
            return "Virtual memory";
        case seL4_Fault_UnknownSyscall:
            return "Unknown syscall";
        case seL4_Fault_UserException:
            return "User exception";
        case seL4_Fault_VGICMaintenance:
            return "VGIC maintenance";
        case seL4_Fault_VCPUFault:
            return "VCPU";
        case seL4_Fault_VPPIEvent:
            return "VPPI event";
        default:
            return "Unknown fault";
    }
}

static seL4_Word id_to_reg_val(seL4_Word reg_id, seL4_UserContext* regs)
{
    switch (reg_id)
    {
        case 0:
            return regs->x0;
        case 1:
            return regs->x1;
        case 2:
            return regs->x2;
        case 3:
            return regs->x3;
        case 4:
            return regs->x4;
        case 5:
            return regs->x5;
        case 6:
            return regs->x6;
        case 7:
            return regs->x7;
        case 8:
            return regs->x8;
        case 9:
            return regs->x9;
        case 10:
            return regs->x10;
        case 11:
            return regs->x11;
        case 12:
            return regs->x12;
        case 13:
            return regs->x13;
        case 14:
            return regs->x14;
        case 15:
            return regs->x15;
        case 16:
            return regs->x16;
        case 17:
            return regs->x17;
        case 18:
            return regs->x18;
        case 19:
            return regs->x19;
        case 20:
            return regs->x20;
        case 21:
            return regs->x21;
        case 22:
            return regs->x22;
        case 23:
            return regs->x23;
        case 24:
            return regs->x24;
        case 25:
            return regs->x25;
        case 26:
            return regs->x26;
        case 27:
            return regs->x27;
        case 28:
            return regs->x28;
        case 29:
            return regs->x29;
        case 30:
            return regs->x30;
        case 31:
            return 0; // WZR
        default:
            LOG_VMM("Failed to decode register id, attempted to access invalid register index 0x%lx\n", reg_id);
            assert(0);
    }
}

static inline void advance_vcpu(size_t vcpu_id, seL4_UserContext* regs)
{
    regs->pc += 4;
    seL4_Error err = seL4_TCB_WriteRegisters(BASE_VM_TCB_CAP + vcpu_id, seL4_False, 0, 1, regs);
    assert(err == seL4_NoError);
}

static bool fault_handle_vm_exception(size_t vcpu_id, uint8_t* mem, enum hvt_hypercall* hypercall_id, void** hypercall_data, seL4_UserContext* regs_at_fault)
{
    uint64_t addr = (uint64_t)microkit_mr_get(seL4_VMFault_Addr);
    uint64_t fsr = (uint64_t)microkit_mr_get(seL4_VMFault_FSR);
    seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
    seL4_Word is_prefetch = seL4_GetMR(seL4_VMFault_PrefetchFault);

    seL4_UserContext regs;
    seL4_Error err = seL4_TCB_ReadRegisters(BASE_VM_TCB_CAP + vcpu_id, false, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &regs);
    assert(err == seL4_NoError);

    uint64_t isv = (fsr >> 24) & 1;
    uint64_t il = (fsr >> 25) & 1;
    uint64_t write = (fsr >> 6) & 1;
    uint64_t src_reg = (fsr >> 16) & 31;
    uint64_t reg_data = (uint64_t)id_to_reg_val(src_reg, &regs);
    enum hvt_hypercall hc = HVT_HYPERCALL_NR(addr);

    // Check if we actually got a hypercall
    if (isv && il && write && hc >= 1 && hc <= HVT_HYPERCALL_MAX)
    {
        // User hypercalls are not expected to be synchronous, for example the hypercall may write to a disk driver and wait for a result and resume through the
        // notified() method
        microkit_vcpu_stop(vcpu_id);

        // Since we are not doing a proper vmexit, we don't have the typical memory coherency guarnetees and need a memory barrier
        atomic_thread_fence(memory_order_acquire);

        *hypercall_id = hc;
        *hypercall_data = (void*)(mem + reg_data);
        if (regs_at_fault) *regs_at_fault = regs;

        advance_vcpu(vcpu_id, &regs);
        // registered_hypercall();

        return true;
    }

    LOG_VMM("Unexpected memory fault on address: 0x%lx, FSR: 0x%lx, IP: 0x%lx, is_prefetch: %s\n", addr, fsr, ip, is_prefetch ? "true" : "false");
    LOG_VMM("instr: 0x%lx 0x%lx 0x%lx 0x%lx\n", *(mem + ip), *(mem + ip + 1), *(mem + ip + 2), *(mem + ip + 3));
    LOG_VMM("fsr: %ld\n", fsr);
    LOG_VMM("valid isv: %ld\n", isv);
    LOG_VMM("valid il: %ld\n", il);
    LOG_VMM("was write: %ld\n", write);
    LOG_VMM("src reg: %ld\n", src_reg);
    LOG_VMM("reg value: %ld\n", reg_data);
    LOG_VMM("mem: %ld\n", mem);
    LOG_VMM("possible hypercall number: %ld\n", (uint64_t)hc);

    return false;
}

static bool fault_handle_user_exception(size_t vcpu_id)
{
    seL4_Word fault_ip = microkit_mr_get(seL4_UserException_FaultIP);
    seL4_Word number = microkit_mr_get(seL4_UserException_Number);
    seL4_Word code = microkit_mr_get(seL4_UserException_Code);

    LOG_VMM("User exception fault - invalid instruction/result at IP: 0x%lx, number: 0x%lx, code: 0x%lx\n", fault_ip, number, code);
    LOG_VMM("Stopping VCPU (ID 0x%lx)", vcpu_id);
    microkit_vcpu_stop(vcpu_id);
    vcpu_print_tcb_regs(vcpu_id);
    vcpu_print_sys_regs(vcpu_id);

    return false;
}

bool fault_handle(
    size_t vcpu_id, microkit_msginfo msginfo, uint8_t* mem, enum hvt_hypercall* hypercall_id, void** hypercall_data, seL4_UserContext* regs_at_fault)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);

    switch (label)
    {
        case seL4_Fault_VMFault:
            return fault_handle_vm_exception(vcpu_id, mem, hypercall_id, hypercall_data, regs_at_fault);
        case seL4_Fault_UserException:
            return fault_handle_user_exception(vcpu_id);
        default:
            LOG_VMM("Unexpected fault at VCPU (ID 0x%lx): %s / 0x%lx\n", vcpu_id, fault_to_string(label), label);
            microkit_vcpu_stop(vcpu_id);
            vcpu_print_tcb_regs(vcpu_id);
            vcpu_print_sys_regs(vcpu_id);
            return false;
    }
}