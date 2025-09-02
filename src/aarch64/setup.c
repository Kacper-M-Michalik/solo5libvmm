#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <microkit.h>
#include <solo5libvmm/vcpu.h>
#include <solo5libvmm/setup.h>
#include <solo5libvmm/util/util.h>

#define HVT_HOST
#include "hvt_abi.h"

static uint64_t aarch64_get_counter_frequency(void)
{
    uint64_t frq;

    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r" (frq):: "memory");

    return frq;
}

//Need to fix
static void aarch64_verify_mem_size(size_t* mem_size) 
{
    size_t mem;
    mem = (*mem_size / AARCH64_GUEST_BLOCK_SIZE) * AARCH64_GUEST_BLOCK_SIZE;
    assert(mem <= *mem_size);

    if (mem == 0) mem = AARCH64_GUEST_BLOCK_SIZE;
    
    assert(mem < AARCH64_MMIO_BASE);
    assert (mem == *mem_size);    
    //if (mem != *mem_size) warnx("adjusting memory to %zu bytes", mem);
    //if (mem > AARCH64_MMIO_BASE) errx(1, "guest memory size %zu bytes exceeds the max size %lu bytes", mem, AARCH64_MMIO_BASE);

    *mem_size = mem;
}

static void aarch64_enable_guest_float(size_t vcpu_id)
{
    seL4_Word data;

    LOG_VMM("Enabling the floating-point and Advanced SIMD registers");
    data = microkit_vcpu_arm_read_reg(vcpu_id, seL4_VCPUReg_CPACR);
    data &= ~(_FPEN_MASK);
    data |= (_FPEN_NOTRAP << _FPEN_SHIFT);    
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_CPACR, data);
}

static void aarch64_enable_guest_mmu(size_t vcpu_id)
{
    LOG_VMM("Setting up Memory Attribute Indirection Register");
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_MAIR, MAIR_EL1_INIT);   

    LOG_VMM("Setting up Translation Control Register");
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_TCR, TCR_EL1_INIT);

    /*
     * Setup Translation Table Base Register 0 EL1. The translation range
     * doesn't exceed the 0 ~ 1^64. So the TTBR0_EL1 is enough.
     */
    LOG_VMM("Setting up Translation Table Base Register 0 EL1");
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_TTBR0, AARCH64_PGD_PGT_BASE);

    /* Enable MMU and I/D Cache for EL1 */
    LOG_VMM("Setting up System Control Register EL1");
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_SCTLR, (_SCTLR_M | _SCTLR_C | _SCTLR_I));
}

/*
 * We will do VA = PA mapping in page table. For simplicity, currently
 * we use minimal 2MB block size and 1 PUD table in page table.
 */
static int aarch64_setup_memory_mapping(uint8_t *mem, uint64_t mem_size)
{
    uint64_t paddr, pmd_paddr;
    uint64_t *pgd = (uint64_t *)(mem + AARCH64_PGD_PGT_BASE);
    uint64_t *pud = (uint64_t *)(mem + AARCH64_PUD_PGT_BASE);
    uint64_t *pmd = (uint64_t *)(mem + AARCH64_PMD_PGT_BASE);
    uint64_t *pte = (uint64_t *)(mem + AARCH64_PTE_PGT_BASE);

    /*
     * In order to keep consistency with x86_64, we limit hvt_hypercall only
     * to support sending 32-bit pointers. So we limit the guest to support
     * only 4GB memory. This will avoid using additional code to guarantee the
     * hypercall parameters are using the memory below 4GB.
     *
     * Address above 4GB is using for MMIO space now. This would be changed
     * easily if the design of hvt_hypercall would be changed in the future.
     */
    assert((mem_size & (AARCH64_GUEST_BLOCK_SIZE -1)) == 0);
    assert(mem_size <= AARCH64_MMIO_BASE);
    assert(mem_size >= AARCH64_GUEST_BLOCK_SIZE);

    /* Zero all page tables */
    memset(pgd, 0, AARCH64_PGD_PGT_SIZE);
    memset(pud, 0, AARCH64_PUD_PGT_SIZE);
    memset(pmd, 0, AARCH64_PMD_PGT_SIZE);
    memset(pte, 0, AARCH64_PTE_PGT_SIZE);

    /* Map first 2MB block in pte table */
    for (paddr = 0; paddr < AARCH64_GUEST_BLOCK_SIZE; paddr += PAGE_SIZE, pte++) 
    {
        /*
         * Leave all pages below AARCH64_PGT_MAP_START unmapped in the guest.
         * This includes the zero page and the guest's page tables.
         */
        if (paddr < AARCH64_PGT_MAP_START)
            continue;

        /*
         * Map the remainder of the pages below AARCH64_GUEST_MIN_BASE
         * as read-only; these are used for input from hvt to the guest
         * only, with the rest reserved for future use.
         */
        if (paddr < AARCH64_GUEST_MIN_BASE)
            *pte = paddr | PROT_PAGE_NORMAL_RO;
        else
            *pte = paddr | PROT_PAGE_NORMAL_EXEC;
    }
    assert(paddr == AARCH64_GUEST_BLOCK_SIZE);

    /* Link pte table to pmd[0] */
    *pmd++ = AARCH64_PTE_PGT_BASE | PGT_DESC_TYPE_TABLE;

    /* Mapping left memory by 2MB block in pmd table */
    for (; paddr < mem_size; paddr += PMD_SIZE, pmd++)
    {
        *pmd = paddr | PROT_SECT_NORMAL_EXEC;
    }
    
    /* Link pmd tables (PMD0, PMD1, PMD2, PMD3) to pud[0] ~ pud[3] */
    pmd_paddr = AARCH64_PMD_PGT_BASE;
    for (paddr = 0; paddr < mem_size; paddr += PUD_SIZE, pud++, pmd_paddr += PAGE_SIZE)
    {
        *pud = pmd_paddr | PGT_DESC_TYPE_TABLE;
    }

    /* RAM address should not exceed MMIO_BASE */
    assert(paddr <= AARCH64_MMIO_BASE);
    
    /* Mapping MMIO */
    pud += ((AARCH64_MMIO_BASE - paddr) >> PUD_SHIFT);
    for (paddr = AARCH64_MMIO_BASE; paddr < AARCH64_MMIO_BASE + AARCH64_MMIO_SZ; paddr += PUD_SIZE, pud++)
    {
        *pud = paddr | PROT_SECT_DEVICE_nGnRE;
    }

    /* Link pud table to pgd[0] */
    *pgd = AARCH64_PUD_PGT_BASE | PGT_DESC_TYPE_TABLE;
}

/*
 * Initialize registers: instruction pointer for our code, addends,
 * and PSTATE flags required by ARM64 architecture.
 * Arguments to the kernel main are passed using the ARM64 calling
 * convention: x0 ~ x7
 */
static void aarch64_setup_core_registers(size_t vcpu_id, struct hvt_boot_info *info)
{
    /* Set default PSTATE flags to SPSR_EL1 */
    LOG_VMM("Initializing spsr[EL1]");
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_SPSR_EL1, AARCH64_PSTATE_INIT);    

    /*
     * Set Stack Pointer for Guest. ARM64 require stack be 16-bytes alignment by default.
     * TODO: CHECK IF -16 IS CORRECT
     */
    LOG_VMM("Initializing spsr[EL1]");
    microkit_vcpu_arm_write_reg(vcpu_id, seL4_VCPUReg_SP_EL1, info->mem_size - 16);        

    //Need to do UserContext here 
    /* Passing hvt_boot_info through x0 */ 
    LOG_VMM("Writing boot info to x0");    
    microkit_vcpu_arm_write_reg(vcpu_id, REG_X0, AARCH64_BOOT_INFO); 
    LOG_VMM("Setting guest reset entry to PC");
    microkit_vcpu_arm_write_reg(vcpu_id, REG_PC, gpa_ep); 
}

void solo5_setup_image(size_t boot_vcpu_id, uintptr_t kernel, size_t kernel_size, uintptr_t virtual_memory_offset, size_t memory_size, size_t max_stack_size, const char* cmdline, const void* mft)
{
    uintptr_t kernel_start = kernel;
    uintptr_t kernel_end = kernel_start + kernel_size;
    
    //may have to change, HVT does its own memory checks too
    //need to add kernel size, max stack, 2 pages (1 for stack safety, 1 for heap) divide by page size round up,
    if (kernel_size + max_stack_size >= memory_size) {
        LOG_VMM_ERR("");
        return 0;
    }

    //TODO: CHANGE
    aarch64_verify_mem_size(&memory_size);

    //elf load or memcpy here

    //We need to allocate or copy info into guest memory
    struct hvt_boot_info info;    
    info.mem_size = memory_size; //0x20000000 is max
    info.cpu_cycle_freq = aarch64_get_counter_frequency();
    info.kernel_end = kernel_end;
    //These need to be memcpys
    info.cmdline = cmdline;
    info.mft = mft;    

    /*
     * Setup aarch64 phys to virt mapping. Currently we only map 4GB for
     * RAM space and 1GB for MMIO space. Although the guest can use up
     * to 1TB address space which we configured in TCR_EL1.
     * 
     * TODO: ADD STACK PROTECTION BASED ON MAX STACK
     */
    setup_memory_mapping(boot_vcpu_id, memory_size);
    enable_guest_float(boot_vcpu_id);
    enable_guest_mmu(boot_vcpu_id);

    /* Sets up UserContext */
    setup_core_registers(boot_vcpu_id);

    // Acctually may not return pointer, we do all setup, start stop literally pauses cpu, does not affect pc
    //return HVT_GUEST_MIN_BASE;
}