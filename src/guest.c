#include <string.h>
#include <stdatomic.h>
#include <stdalign.h>
#include <microkit.h>
#include <solo5libvmm/guest.h>
#include <solo5libvmm/elf.h>
#include <solo5libvmm/util.h>
#include <solo5libvmm/aarch64/vcpu.h>
#include <solo5libvmm/solo5/hvt_abi.h>
#include <solo5libvmm/solo5/mft_abi.h>
#include <solo5libvmm/solo5/elf_abi.h>

#define NOTE_BUF_ALIGN alignof(struct mft) 
#define NOTE_BUF_SIZE MFT1_NOTE_MAX_SIZE

_Static_assert(alignof(struct mft) >= alignof(struct abi1_info));
_Static_assert(MFT1_NOTE_MAX_SIZE >= sizeof(struct abi1_info));

void guest_resume(size_t vcpu_id) 
{
    // Make sure any writes done to guest memory are observable by guest
    atomic_thread_fence(memory_order_release);

    //LOG_VMM("Resuming guest\n");
    seL4_Error err;
    seL4_UserContext ctxt = {0};
    err = seL4_TCB_WriteRegisters(
        BASE_VM_TCB_CAP + vcpu_id,
        seL4_True,
        0,
        0,
        &ctxt
    );
    assert(err == seL4_NoError);
    //LOG_VMM("Resumed guest!\n");
}

void guest_stop(size_t vcpu_id) 
{
    LOG_VMM("Stopping guest\n");
    microkit_vcpu_stop(vcpu_id);
    LOG_VMM("Stopped guest\n");
}

void guest_clear(size_t vcpu_id, uint8_t* mem, size_t mem_size) 
{
    LOG_VMM("Stopping guest\n");
    microkit_vcpu_stop(vcpu_id);

    LOG_VMM("Clearing guest RAM\n");
    memset(mem, 0, mem_size);

    LOG_VMM("Resetting guest registers\n");
    vcpu_reset_regs(vcpu_id);

    LOG_VMM("Guest reset\n");
}

bool guest_setup(size_t vcpu_id, uint8_t* kernel, size_t kernel_size, uint8_t* mem, size_t mem_size, size_t max_stack_size, char* cmdline, size_t cmdline_len)
{
    LOG_VMM("Started guest setup\n");

    const size_t MEM_SIZE_ALIGN = AARCH64_GUEST_BLOCK_SIZE;

    //TODO: Check max stack is reasonable and doesnt overlap text/min heap
    if (vcpu_id != 0) 
    {
        LOG_VMM("Invalid vcpu_id, solo5 is single-threaded and only 1 VM allowed per VMM, vcpu_id should be 0\n");
        return false;
    }
    if (cmdline_len > HVT_CMDLINE_SIZE)
    {
        LOG_VMM("cmdline longer than max: %ld (len=%ld)\n", HVT_CMDLINE_SIZE, cmdline_len);
        return false;
    }

    assert(MEM_SIZE_ALIGN % 16 == 0);
    if (mem_size % MEM_SIZE_ALIGN != 0)
    {
        size_t new_mem_size = (mem_size / MEM_SIZE_ALIGN) * MEM_SIZE_ALIGN;
        LOG_VMM("mem_size truncated DOWN to %ld byte alignment (old=%ld new=%ld)\n", MEM_SIZE_ALIGN, mem_size, new_mem_size);
        mem_size = new_mem_size;
    }   
    if (mem_size == 0)
    {
        LOG_VMM("mem_size too small (required=%ld mem_size=%ld)", MEM_SIZE_ALIGN, mem_size);
        return false;
    }

    alignas(NOTE_BUF_ALIGN) uint8_t note_buf[NOTE_BUF_SIZE]; 
    size_t acc_note_size;    
    
    struct abi1_info* elf_abi = (struct abi1_info*)note_buf;
    if (!elf_load_note(kernel, kernel_size, ABI1_NOTE_TYPE, ABI1_NOTE_ALIGN, ABI1_NOTE_MAX_SIZE, note_buf, &acc_note_size))
    {
        LOG_VMM("Missing or invalid ABI note\n");
        return false;
    }
    if (elf_abi->abi_target != HVT_ABI_TARGET)
    {
        LOG_VMM("Wrong target (non-HVT)\n");
        return false;
    }
    if (elf_abi->abi_version != HVT_ABI_VERSION)
    {
        LOG_VMM("Wrong HVT version (supported ver=%ld) (target ver=%ld)\n", HVT_ABI_VERSION, elf_abi->abi_version);
        return false;
    }

    struct mft* elf_mft = (struct mft*)note_buf;
    if (!elf_load_note(kernel, kernel_size, MFT1_NOTE_TYPE, MFT1_NOTE_ALIGN, MFT1_NOTE_MAX_SIZE, note_buf, &acc_note_size))
    {
        LOG_VMM("Missing or invalid MFT note\n");
        return false;
    }
    if (elf_mft->entries < 1)
    {
        LOG_VMM("Invalid number of MFT entries (entries<1) - something has gone wrong with note loading\n");
        return false;
    }
    LOG_VMM("MFT entries: %ld\n", elf_mft->entries);
    LOG_VMM("MFT ver: %ld\n", elf_mft->version);    
    LOG_VMM("MFT Entry 0\n");
    LOG_VMM("Name: RESERVED\n");
    LOG_VMM("Type: RESERVED\n");
    for (uint64_t i = 1; i < elf_mft->entries; i++)
    {
        LOG_VMM("MFT Entry %ld\n", i);
        LOG_VMM("Name: %s\n", elf_mft->e[i].name);
        LOG_VMM("Type: %ld\n", elf_mft->e[i].type);
    }

    LOG_VMM("guest_setup passed arg checks\n");

    //TODO: Add protection propagation
    uint64_t p_entry;
    uint64_t p_end;
    if (!elf_load(kernel, kernel_size, mem, mem_size, AARCH64_GUEST_MIN_BASE, &p_entry, &p_end))
    {
        LOG_VMM("Failed to load HVT file (incompatible or invalid)\n");
        return false;
    }

    LOG_VMM("Loaded elf\n");
    LOG_VMM("p_entry: %zu\n", p_entry);
    LOG_VMM("p_end: %zu\n", p_end);

    // Allocate boot info in guest memory, and verify alignment
    struct hvt_boot_info* info = (struct hvt_boot_info*)((uint64_t)mem + AARCH64_BOOT_INFO);
    assert(((uint64_t)info % _Alignof(struct hvt_boot_info)) == 0);  

    info->mem_size = mem_size;
    info->cpu_cycle_freq = aarch64_get_counter_frequency();
    info->kernel_end = p_end;

    // Copy in cmdline
    uint64_t arg_ptr = (uint64_t)info + sizeof(struct hvt_boot_info);
    memcpy((uint8_t*)arg_ptr, cmdline, cmdline_len);
    *((char*)(arg_ptr + cmdline_len)) = '\0';
    info->cmdline = arg_ptr - (uint64_t)mem; 
    arg_ptr += cmdline_len + 1;

    //TODO: Update MFT attached fields
    // Copy in MFT  
    arg_ptr = ((arg_ptr / alignof(struct mft)) + 1) * alignof(struct mft); // Align arg_pointer to next mft align   
    memcpy((uint8_t*)arg_ptr, note_buf, acc_note_size);     
    info->mft = arg_ptr - (uint64_t)mem;
    arg_ptr += acc_note_size;

    // Check arguments fit in space and don't overlap text
    if (arg_ptr - (uint64_t)mem > AARCH64_GUEST_MIN_BASE) 
    {
        LOG_VMM("cmdline + mft args too long - overwrite program text\n");
        return false;
    }

    LOG_VMM("mem addr : %zu\n", mem);  
    LOG_VMM("mem_size: %zu\n", info->mem_size);
    LOG_VMM("boot_info guest addr: %zu\n", (uint64_t)(info) - (uint64_t)mem);
    LOG_VMM("cpu_cycle_freq: %zu\n", info->cpu_cycle_freq);
    LOG_VMM("kernel_end guest addr: %zu\n", info->kernel_end);
    LOG_VMM("cmdline guest addr: %zu\n", info->cmdline);
    LOG_VMM("mft guest addr: %zu\n", info->mft);

    // Add arch IFDEFS here, if you want to support more archs in the future

    //TODO: Add stack protection based on max stack
    setup_memory_mapping(mem, mem_size);
    setup_system_registers(vcpu_id, mem_size);
    setup_tcb_registers(vcpu_id, p_entry, AARCH64_BOOT_INFO);

    return true;
}