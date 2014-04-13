//===-- DNBArchMachARM64.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/25/07.
//
//===----------------------------------------------------------------------===//

#if defined (__arm__) || defined (__arm64__)

#include "MacOSX/arm64/DNBArchImplARM64.h"

#if defined (ARM_THREAD_STATE64_COUNT)

#include "MacOSX/MachProcess.h"
#include "MacOSX/MachThread.h"
#include "DNBBreakpoint.h"
#include "DNBLog.h"
#include "DNBRegisterInfo.h"
#include "DNB.h"

#include <inttypes.h>
#include <sys/sysctl.h>

// Break only in privileged or user mode
// (PAC bits in the DBGWVRn_EL1 watchpoint control register)
#define S_USER                  ((uint32_t)(2u << 1))

#define BCR_ENABLE              ((uint32_t)(1u))
#define WCR_ENABLE              ((uint32_t)(1u))

// Watchpoint load/store
// (LSC bits in the DBGWVRn_EL1 watchpoint control register)
#define WCR_LOAD                ((uint32_t)(1u << 3))
#define WCR_STORE               ((uint32_t)(1u << 4))

// Enable breakpoint, watchpoint, and vector catch debug exceptions.
// (MDE bit in the MDSCR_EL1 register.  Equivalent to the MDBGen bit in DBGDSCRext in Aarch32)
#define MDE_ENABLE ((uint32_t)(1u << 15))

// Single instruction step
// (SS bit in the MDSCR_EL1 register)
#define SS_ENABLE ((uint32_t)(1u))

static const uint8_t g_arm64_breakpoint_opcode[] = { 0x00, 0x00, 0x20, 0xD4 }; // "brk #0", 0xd4200000 in BE byte order
static const uint8_t g_arm_breakpoint_opcode[] = { 0xFE, 0xDE, 0xFF, 0xE7 };   // this armv7 insn also works in arm64

// If we need to set one logical watchpoint by using
// two hardware watchpoint registers, the watchpoint
// will be split into a "high" and "low" watchpoint.
// Record both of them in the LoHi array.

// It's safe to initialize to all 0's since 
// hi > lo and therefore LoHi[i] cannot be 0.
static uint32_t LoHi[16] = { 0 };


void
DNBArchMachARM64::Initialize()
{
    DNBArchPluginInfo arch_plugin_info = 
    {
        CPU_TYPE_ARM64, 
        DNBArchMachARM64::Create, 
        DNBArchMachARM64::GetRegisterSetInfo,
        DNBArchMachARM64::SoftwareBreakpointOpcode
    };
    
    // Register this arch plug-in with the main protocol class
    DNBArchProtocol::RegisterArchPlugin (arch_plugin_info);
}


DNBArchProtocol *
DNBArchMachARM64::Create (MachThread *thread)
{
    DNBArchMachARM64 *obj = new DNBArchMachARM64 (thread);

    return obj;
}

const uint8_t * const
DNBArchMachARM64::SoftwareBreakpointOpcode (nub_size_t byte_size)
{
    return g_arm_breakpoint_opcode;
}

uint32_t
DNBArchMachARM64::GetCPUType()
{
    return CPU_TYPE_ARM64;
}

uint64_t
DNBArchMachARM64::GetPC(uint64_t failValue)
{
    // Get program counter
    if (GetGPRState(false) == KERN_SUCCESS)
        return m_state.context.gpr.__pc;
    return failValue;
}

kern_return_t
DNBArchMachARM64::SetPC(uint64_t value)
{
    // Get program counter
    kern_return_t err = GetGPRState(false);
    if (err == KERN_SUCCESS)
    {
        m_state.context.gpr.__pc = value;
        err = SetGPRState();
    }
    return err == KERN_SUCCESS;
}

uint64_t
DNBArchMachARM64::GetSP(uint64_t failValue)
{
    // Get stack pointer
    if (GetGPRState(false) == KERN_SUCCESS)
        return m_state.context.gpr.__sp;
    return failValue;
}

kern_return_t
DNBArchMachARM64::GetGPRState(bool force)
{
    int set = e_regSetGPR;
    // Check if we have valid cached registers
    if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
        return KERN_SUCCESS;

    // Read the registers from our thread
    mach_msg_type_number_t count = e_regSetGPRCount;
    kern_return_t kret = ::thread_get_state(m_thread->MachPortNumber(), ARM_THREAD_STATE64, (thread_state_t)&m_state.context.gpr, &count);
    if (DNBLogEnabledForAny (LOG_THREAD))
    {
        uint64_t *x = &m_state.context.gpr.__x[0];
        DNBLogThreaded("thread_get_state(0x%4.4x, %u, &gpr, %u) => 0x%8.8x (count = %u) regs"
                       "\n   x0=%16.16llx"
                       "\n   x1=%16.16llx"
                       "\n   x2=%16.16llx"
                       "\n   x3=%16.16llx"
                       "\n   x4=%16.16llx"
                       "\n   x5=%16.16llx"
                       "\n   x6=%16.16llx"
                       "\n   x7=%16.16llx"
                       "\n   x8=%16.16llx"
                       "\n   x9=%16.16llx"
                       "\n  x10=%16.16llx"
                       "\n  x11=%16.16llx"
                       "\n  x12=%16.16llx"
                       "\n  x13=%16.16llx"
                       "\n  x14=%16.16llx"
                       "\n  x15=%16.16llx"
                       "\n  x16=%16.16llx"
                       "\n  x17=%16.16llx"
                       "\n  x18=%16.16llx"
                       "\n  x19=%16.16llx"
                       "\n  x20=%16.16llx"
                       "\n  x21=%16.16llx"
                       "\n  x22=%16.16llx"
                       "\n  x23=%16.16llx"
                       "\n  x24=%16.16llx"
                       "\n  x25=%16.16llx"
                       "\n  x26=%16.16llx"
                       "\n  x27=%16.16llx"
                       "\n  x28=%16.16llx"
                       "\n   fp=%16.16llx"
                       "\n   lr=%16.16llx"
                       "\n   sp=%16.16llx"
                       "\n   pc=%16.16llx"
                       "\n cpsr=%8.8x", 
                       m_thread->MachPortNumber(), 
                       e_regSetGPR, 
                       e_regSetGPRCount, 
                       kret,
                       count,
                       x[0], 
                       x[1], 
                       x[2], 
                       x[3], 
                       x[4], 
                       x[5], 
                       x[6], 
                       x[7], 
                       x[8], 
                       x[9], 
                       x[0], 
                       x[11], 
                       x[12], 
                       x[13], 
                       x[14], 
                       x[15], 
                       x[16], 
                       x[17], 
                       x[18], 
                       x[19], 
                       x[20], 
                       x[21], 
                       x[22], 
                       x[23], 
                       x[24], 
                       x[25], 
                       x[26], 
                       x[27], 
                       x[28], 
                       m_state.context.gpr.__fp,
                       m_state.context.gpr.__lr,
                       m_state.context.gpr.__sp,
                       m_state.context.gpr.__pc,
                       m_state.context.gpr.__cpsr);
    }
    m_state.SetError(set, Read, kret);
    return kret;
}

kern_return_t
DNBArchMachARM64::GetVFPState(bool force)
{
    int set = e_regSetVFP;
    // Check if we have valid cached registers
    if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
        return KERN_SUCCESS;

    // Read the registers from our thread
    mach_msg_type_number_t count = e_regSetVFPCount;
    kern_return_t kret = ::thread_get_state(m_thread->MachPortNumber(), ARM_NEON_STATE64, (thread_state_t)&m_state.context.vfp, &count);
    if (DNBLogEnabledForAny (LOG_THREAD))
    {
#if defined (__arm64__)
        uint64_t d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12, d13, d14, d15, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31;
        memcpy (&d0, &m_state.context.vfp.__v[0], 8);
        memcpy (&d1, &m_state.context.vfp.__v[1], 8);
        memcpy (&d2, &m_state.context.vfp.__v[2], 8);
        memcpy (&d3, &m_state.context.vfp.__v[3], 8);
        memcpy (&d4, &m_state.context.vfp.__v[4], 8);
        memcpy (&d5, &m_state.context.vfp.__v[5], 8);
        memcpy (&d6, &m_state.context.vfp.__v[6], 8);
        memcpy (&d7, &m_state.context.vfp.__v[7], 8);
        memcpy (&d8, &m_state.context.vfp.__v[8], 8);
        memcpy (&d9, &m_state.context.vfp.__v[9], 8);
        memcpy (&d10, &m_state.context.vfp.__v[10], 8);
        memcpy (&d11, &m_state.context.vfp.__v[11], 8);
        memcpy (&d12, &m_state.context.vfp.__v[12], 8);
        memcpy (&d13, &m_state.context.vfp.__v[13], 8);
        memcpy (&d14, &m_state.context.vfp.__v[14], 8);
        memcpy (&d15, &m_state.context.vfp.__v[15], 8);
        memcpy (&d16, &m_state.context.vfp.__v[16], 8);
        memcpy (&d17, &m_state.context.vfp.__v[17], 8);
        memcpy (&d18, &m_state.context.vfp.__v[18], 8);
        memcpy (&d19, &m_state.context.vfp.__v[19], 8);
        memcpy (&d20, &m_state.context.vfp.__v[20], 8);
        memcpy (&d21, &m_state.context.vfp.__v[21], 8);
        memcpy (&d22, &m_state.context.vfp.__v[22], 8);
        memcpy (&d23, &m_state.context.vfp.__v[23], 8);
        memcpy (&d24, &m_state.context.vfp.__v[24], 8);
        memcpy (&d25, &m_state.context.vfp.__v[25], 8);
        memcpy (&d26, &m_state.context.vfp.__v[26], 8);
        memcpy (&d27, &m_state.context.vfp.__v[27], 8);
        memcpy (&d28, &m_state.context.vfp.__v[28], 8);
        memcpy (&d29, &m_state.context.vfp.__v[29], 8);
        memcpy (&d30, &m_state.context.vfp.__v[30], 8);
        memcpy (&d31, &m_state.context.vfp.__v[31], 8);
        DNBLogThreaded("thread_get_state(0x%4.4x, %u, &vfp, %u) => 0x%8.8x (count = %u) regs"
                       "\n   d0=%16.16llx"
                       "\n   d1=%16.16llx"
                       "\n   d2=%16.16llx"
                       "\n   d3=%16.16llx"
                       "\n   d4=%16.16llx"
                       "\n   d5=%16.16llx"
                       "\n   d6=%16.16llx"
                       "\n   d7=%16.16llx"
                       "\n   d8=%16.16llx"
                       "\n   d9=%16.16llx"
                       "\n   d10=%16.16llx"
                       "\n   d11=%16.16llx"
                       "\n   d12=%16.16llx"
                       "\n   d13=%16.16llx"
                       "\n   d14=%16.16llx"
                       "\n   d15=%16.16llx"
                       "\n   d16=%16.16llx"
                       "\n   d17=%16.16llx"
                       "\n   d18=%16.16llx"
                       "\n   d19=%16.16llx"
                       "\n   d20=%16.16llx"
                       "\n   d21=%16.16llx"
                       "\n   d22=%16.16llx"
                       "\n   d23=%16.16llx"
                       "\n   d24=%16.16llx"
                       "\n   d25=%16.16llx"
                       "\n   d26=%16.16llx"
                       "\n   d27=%16.16llx"
                       "\n   d28=%16.16llx"
                       "\n   d29=%16.16llx"
                       "\n   d30=%16.16llx"
                       "\n   d31=%16.16llx",
                       m_thread->MachPortNumber(), 
                       e_regSetVFP, 
                       e_regSetVFPCount, 
                       kret,
                       count,
                       d0,
                       d1, 
                       d2,
                       d3,
                       d4,
                       d5,
                       d6,
                       d7,
                       d8,
                       d9,
                       d10,
                       d11,
                       d12,
                       d13,
                       d14,
                       d15,
                       d16,
                       d17,
                       d18,
                       d19,
                       d20,
                       d21,
                       d22,
                       d23,
                       d24,
                       d25,
                       d26,
                       d27,
                       d28,
                       d29,
                       d30,
                       d31);
#endif
    }
    m_state.SetError(set, Read, kret);
    return kret;
}

kern_return_t
DNBArchMachARM64::GetEXCState(bool force)
{
    int set = e_regSetEXC;
    // Check if we have valid cached registers
    if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
        return KERN_SUCCESS;

    // Read the registers from our thread
    mach_msg_type_number_t count = e_regSetEXCCount;
    kern_return_t kret = ::thread_get_state(m_thread->MachPortNumber(), ARM_EXCEPTION_STATE64, (thread_state_t)&m_state.context.exc, &count);
    m_state.SetError(set, Read, kret);
    return kret;
}

static void
DumpDBGState(const arm_debug_state_t& dbg)
{
    uint32_t i = 0;
    for (i=0; i<16; i++)
        DNBLogThreadedIf(LOG_STEP, "BVR%-2u/BCR%-2u = { 0x%8.8x, 0x%8.8x } WVR%-2u/WCR%-2u = { 0x%8.8x, 0x%8.8x }",
            i, i, dbg.__bvr[i], dbg.__bcr[i],
            i, i, dbg.__wvr[i], dbg.__wcr[i]);
}

kern_return_t
DNBArchMachARM64::GetDBGState(bool force)
{
    int set = e_regSetDBG;

    // Check if we have valid cached registers
    if (!force && m_state.GetError(set, Read) == KERN_SUCCESS)
        return KERN_SUCCESS;

    // Read the registers from our thread
    mach_msg_type_number_t count = e_regSetDBGCount;
    kern_return_t kret = ::thread_get_state(m_thread->MachPortNumber(), ARM_DEBUG_STATE64, (thread_state_t)&m_state.dbg, &count);
    m_state.SetError(set, Read, kret);

    return kret;
}

kern_return_t
DNBArchMachARM64::SetGPRState()
{
    int set = e_regSetGPR;
    kern_return_t kret = ::thread_set_state(m_thread->MachPortNumber(), ARM_THREAD_STATE64, (thread_state_t)&m_state.context.gpr, e_regSetGPRCount);
    m_state.SetError(set, Write, kret);         // Set the current write error for this register set
    m_state.InvalidateRegisterSetState(set);    // Invalidate the current register state in case registers are read back differently
    return kret;                                // Return the error code
}

kern_return_t
DNBArchMachARM64::SetVFPState()
{
    int set = e_regSetVFP;
    kern_return_t kret = ::thread_set_state (m_thread->MachPortNumber(), ARM_NEON_STATE64, (thread_state_t)&m_state.context.vfp, e_regSetVFPCount);
    m_state.SetError(set, Write, kret);         // Set the current write error for this register set
    m_state.InvalidateRegisterSetState(set);    // Invalidate the current register state in case registers are read back differently
    return kret;                                // Return the error code
}

kern_return_t
DNBArchMachARM64::SetEXCState()
{
    return KERN_SUCCESS;                        // skip everything <rdar://problem/12443935>
    
    int set = e_regSetEXC;
    kern_return_t kret = ::thread_set_state (m_thread->MachPortNumber(), ARM_EXCEPTION_STATE64, (thread_state_t)&m_state.context.exc, e_regSetEXCCount);
    m_state.SetError(set, Write, kret);         // Set the current write error for this register set
    m_state.InvalidateRegisterSetState(set);    // Invalidate the current register state in case registers are read back differently
    return kret;                                // Return the error code
}

kern_return_t
DNBArchMachARM64::SetDBGState(bool also_set_on_task)
{
    int set = e_regSetDBG;
    kern_return_t kret = ::thread_set_state (m_thread->MachPortNumber(), ARM_DEBUG_STATE64, (thread_state_t)&m_state.dbg, e_regSetDBGCount);
    if (also_set_on_task)
    {
        kern_return_t task_kret = task_set_state (m_thread->Process()->Task().TaskPort(), ARM_DEBUG_STATE64, (thread_state_t)&m_state.dbg, e_regSetDBGCount);
        if (task_kret != KERN_SUCCESS)
             DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::SetDBGState failed to set debug control register state: 0x%8.8x.", task_kret);
    }
    m_state.SetError(set, Write, kret);         // Set the current write error for this register set
    m_state.InvalidateRegisterSetState(set);    // Invalidate the current register state in case registers are read back differently

    return kret;                                // Return the error code
}

void
DNBArchMachARM64::ThreadWillResume()
{
    // Do we need to step this thread? If so, let the mach thread tell us so.
    if (m_thread->IsStepping())
    {
        EnableHardwareSingleStep(true);
    }

    // Disable the triggered watchpoint temporarily before we resume.
    // Plus, we try to enable hardware single step to execute past the instruction which triggered our watchpoint.
    if (m_watchpoint_did_occur)
    {
        if (m_watchpoint_hw_index >= 0)
        {
            kern_return_t kret = GetDBGState(false);
            if (kret == KERN_SUCCESS && !IsWatchpointEnabled(m_state.dbg, m_watchpoint_hw_index)) {
                // The watchpoint might have been disabled by the user.  We don't need to do anything at all
                // to enable hardware single stepping.
                m_watchpoint_did_occur = false;
                m_watchpoint_hw_index = -1;
                return;
            }

            DisableHardwareWatchpoint(m_watchpoint_hw_index, false);
            DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM::ThreadWillResume() DisableHardwareWatchpoint(%d) called",
                             m_watchpoint_hw_index);

            // Enable hardware single step to move past the watchpoint-triggering instruction.
            m_watchpoint_resume_single_step_enabled = (EnableHardwareSingleStep(true) == KERN_SUCCESS);

            // If we are not able to enable single step to move past the watchpoint-triggering instruction,
            // at least we should reset the two watchpoint member variables so that the next time around
            // this callback function is invoked, the enclosing logical branch is skipped.
            if (!m_watchpoint_resume_single_step_enabled) {
                // Reset the two watchpoint member variables.
                m_watchpoint_did_occur = false;
                m_watchpoint_hw_index = -1;
                DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM::ThreadWillResume() failed to enable single step");
            }
            else
                DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM::ThreadWillResume() succeeded to enable single step");
        }
    }
}

bool
DNBArchMachARM64::NotifyException(MachException::Data& exc)
{

    switch (exc.exc_type)
    {
        default:
            break;
        case EXC_BREAKPOINT:
            if (exc.exc_data.size() == 2 && exc.exc_data[0] == EXC_ARM_DA_DEBUG)
            {
                // The data break address is passed as exc_data[1].
                nub_addr_t addr = exc.exc_data[1];
                // Find the hardware index with the side effect of possibly massaging the
                // addr to return the starting address as seen from the debugger side.
                uint32_t hw_index = GetHardwareWatchpointHit(addr);

                // One logical watchpoint was split into two watchpoint locations because
                // it was too big.  If the watchpoint exception is indicating the 2nd half
                // of the two-parter, find the address of the 1st half and report that --
                // that's what lldb is going to expect to see.
                DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM::NotifyException watchpoint %d was hit on address 0x%llx", hw_index, (uint64_t) addr);
                const int num_watchpoints = NumSupportedHardwareWatchpoints ();
                for (int i = 0; i < num_watchpoints; i++)
                {
                   if (LoHi[i] != 0
                       && LoHi[i] == hw_index 
                       && LoHi[i] != i
                       && GetWatchpointAddressByIndex (i) != INVALID_NUB_ADDRESS)
                   {
                       addr = GetWatchpointAddressByIndex (i);
                       DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM::NotifyException It is a linked watchpoint; rewritten to index %d addr 0x%llx", LoHi[i], (uint64_t) addr);
                    }
                }

                if (hw_index != INVALID_NUB_HW_INDEX)
                {
                    m_watchpoint_did_occur = true;
                    m_watchpoint_hw_index = hw_index;
                    exc.exc_data[1] = addr;
                    // Piggyback the hw_index in the exc.data.
                    exc.exc_data.push_back(hw_index);
                }

                return true;
            }
            break;
    }
    return false;
}

bool
DNBArchMachARM64::ThreadDidStop()
{
    bool success = true;
    
    m_state.InvalidateAllRegisterStates();

    if (m_watchpoint_resume_single_step_enabled)
    {
        // Great!  We now disable the hardware single step as well as re-enable the hardware watchpoint.
        // See also ThreadWillResume().
        if (EnableHardwareSingleStep(false) == KERN_SUCCESS)
        {
            if (m_watchpoint_did_occur && m_watchpoint_hw_index >= 0)
            {
                ReenableHardwareWatchpoint(m_watchpoint_hw_index);
                m_watchpoint_resume_single_step_enabled = false;
                m_watchpoint_did_occur = false;
                m_watchpoint_hw_index = -1;
            }
            else
            {
                DNBLogError("internal error detected: m_watchpoint_resume_step_enabled is true but (m_watchpoint_did_occur && m_watchpoint_hw_index >= 0) does not hold!");
            }
        }
        else
        {
            DNBLogError("internal error detected: m_watchpoint_resume_step_enabled is true but unable to disable single step!");
        }
    }

    // Are we stepping a single instruction?
    if (GetGPRState(true) == KERN_SUCCESS)
    {
        // We are single stepping, was this the primary thread?
        if (m_thread->IsStepping())
        {
            // This was the primary thread, we need to clear the trace
            // bit if so.
            success = EnableHardwareSingleStep(false) == KERN_SUCCESS;
        }
        else
        {
            // The MachThread will automatically restore the suspend count
            // in ThreadDidStop(), so we don't need to do anything here if
            // we weren't the primary thread the last time
        }
    }
    return success;
}

// Set the single step bit in the processor status register.
kern_return_t
DNBArchMachARM64::EnableHardwareSingleStep (bool enable)
{
    DNBError err;
    DNBLogThreadedIf(LOG_STEP, "%s( enable = %d )", __FUNCTION__, enable);

    err = GetGPRState(false);

    if (err.Fail())
    {
        err.LogThreaded("%s: failed to read the GPR registers", __FUNCTION__);
        return err.Error();
    }

    err = GetDBGState(false);

    if (err.Fail())
    {
        err.LogThreaded("%s: failed to read the DBG registers", __FUNCTION__);
        return err.Error();
    }

    if (enable)
    {
        DNBLogThreadedIf(LOG_STEP, "%s: Setting MDSCR_EL1 Single Step bit at pc 0x%llx", __FUNCTION__, (uint64_t) m_state.context.gpr.__pc);
        m_state.dbg.__mdscr_el1 |= SS_ENABLE;
    }
    else
    {
        DNBLogThreadedIf(LOG_STEP, "%s: Clearing MDSCR_EL1 Single Step bit at pc 0x%llx", __FUNCTION__, (uint64_t) m_state.context.gpr.__pc);
        m_state.dbg.__mdscr_el1 &= ~(SS_ENABLE);
    }

    return SetDBGState(false);
}

// return 1 if bit "BIT" is set in "value"
static inline uint32_t bit(uint32_t value, uint32_t bit)
{
    return (value >> bit) & 1u;
}

// return the bitfield "value[msbit:lsbit]".
static inline uint64_t bits(uint64_t value, uint32_t msbit, uint32_t lsbit)
{
    assert(msbit >= lsbit);
    uint64_t shift_left = sizeof(value) * 8 - 1 - msbit;
    value <<= shift_left;           // shift anything above the msbit off of the unsigned edge
    value >>= shift_left + lsbit;   // shift it back again down to the lsbit (including undoing any shift from above)
    return value;                   // return our result
}

uint32_t
DNBArchMachARM64::NumSupportedHardwareWatchpoints()
{
    // Set the init value to something that will let us know that we need to
    // autodetect how many watchpoints are supported dynamically...
    static uint32_t g_num_supported_hw_watchpoints = UINT_MAX;
    if (g_num_supported_hw_watchpoints == UINT_MAX)
    {
        // Set this to zero in case we can't tell if there are any HW breakpoints
        g_num_supported_hw_watchpoints = 0;
        
        
        size_t len;
        uint32_t n = 0;
        len = sizeof (n);
        if (::sysctlbyname("hw.optional.watchpoint", &n, &len, NULL, 0) == 0)
        {
            g_num_supported_hw_watchpoints = n;
            DNBLogThreadedIf(LOG_THREAD, "hw.optional.watchpoint=%u", n);
        }
        else
        {
            // For AArch64 we would need to look at ID_AA64DFR0_EL1 but debugserver runs in EL0 so it can't
            // access that reg.  The kernel should have filled in the sysctls based on it though.
#if defined (__arm__)
            uint32_t register_DBGDIDR;

            asm("mrc p14, 0, %0, c0, c0, 0" : "=r" (register_DBGDIDR));
            uint32_t numWRPs = bits(register_DBGDIDR, 31, 28);
            // Zero is reserved for the WRP count, so don't increment it if it is zero
            if (numWRPs > 0)
                numWRPs++;
            g_num_supported_hw_watchpoints = numWRPs;
            DNBLogThreadedIf(LOG_THREAD, "Number of supported hw watchpoints via asm():  %d", g_num_supported_hw_watchpoints);
#endif
        }
    }
    return g_num_supported_hw_watchpoints;
}

uint32_t
DNBArchMachARM64::EnableHardwareWatchpoint (nub_addr_t addr, nub_size_t size, bool read, bool write, bool also_set_on_task)
{
    DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::EnableHardwareWatchpoint(addr = 0x%8.8llx, size = %zu, read = %u, write = %u)", (uint64_t)addr, size, read, write);

    const uint32_t num_hw_watchpoints = NumSupportedHardwareWatchpoints();

    // Can't watch zero bytes
    if (size == 0)
        return INVALID_NUB_HW_INDEX;

    // We must watch for either read or write
    if (read == false && write == false)
        return INVALID_NUB_HW_INDEX;

    // Otherwise, can't watch more than 8 bytes per WVR/WCR pair
    if (size > 8)
        return INVALID_NUB_HW_INDEX;

    // arm64 watchpoints really have an 8-byte alignment requirement.  You can put a watchpoint on a 4-byte
    // offset address but you can only watch 4 bytes with that watchpoint.

    // arm64 watchpoints on an 8-byte (double word) aligned addr can watch any bytes in that 
    // 8-byte long region of memory.  They can watch the 1st byte, the 2nd byte, 3rd byte, etc, or any
    // combination therein by setting the bits in the BAS [12:5] (Byte Address Select) field of
    // the DBGWCRn_EL1 reg for the watchpoint.

    // If the MASK [28:24] bits in the DBGWCRn_EL1 allow a single watchpoint to monitor a larger region
    // of memory (16 bytes, 32 bytes, or 2GB) but the Byte Address Select bitfield then selects a larger
    // range of bytes, instead of individual bytes.  See the ARMv8 Debug Architecture manual for details.
    // This implementation does not currently use the MASK bits; the largest single region watched by a single
    // watchpoint right now is 8-bytes.

    nub_addr_t aligned_wp_address = addr & ~0x7;
    uint32_t addr_dword_offset = addr & 0x7;

    // Do we need to split up this logical watchpoint into two hardware watchpoint
    // registers?
    // e.g. a watchpoint of length 4 on address 6.  We need do this with
    //   one watchpoint on address 0 with bytes 6 & 7 being monitored
    //   one watchpoint on address 8 with bytes 0, 1, 2, 3 being monitored

    if (addr_dword_offset + size > 8)
    {
        DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::EnableHardwareWatchpoint(addr = 0x%8.8llx, size = %zu) needs two hardware watchpoints slots to monitor", (uint64_t)addr, size);
        int low_watchpoint_size = 8 - addr_dword_offset;
        int high_watchpoint_size = addr_dword_offset + size - 8;

        uint32_t lo = EnableHardwareWatchpoint(addr, low_watchpoint_size, read, write, also_set_on_task);
        if (lo == INVALID_NUB_HW_INDEX)
            return INVALID_NUB_HW_INDEX;
        uint32_t hi = EnableHardwareWatchpoint (aligned_wp_address + 8, high_watchpoint_size, read, write, also_set_on_task);
        if (hi == INVALID_NUB_HW_INDEX)
        {
            DisableHardwareWatchpoint (lo, also_set_on_task);
            return INVALID_NUB_HW_INDEX;
        }
        // Tag this lo->hi mapping in our database.
        LoHi[lo] = hi;
        return lo;
    }

    // At this point 
    //  1 aligned_wp_address is the requested address rounded down to 8-byte alignment
    //  2 addr_dword_offset is the offset into that double word (8-byte) region that we are watching
    //  3 size is the number of bytes within that 8-byte region that we are watching

    // Set the Byte Address Selects bits DBGWCRn_EL1 bits [12:5] based on the above.
    // The bit shift and negation operation will give us 0b11 for 2, 0b1111 for 4, etc, up to 0b11111111 for 8.
    // then we shift those bits left by the offset into this dword that we are interested in.
    // e.g. if we are watching bytes 4,5,6,7 in a dword we want a BAS of 0b11110000.
    uint32_t byte_address_select = ((1 << size) - 1) << addr_dword_offset;

    // Read the debug state
    kern_return_t kret = GetDBGState(false);

    if (kret == KERN_SUCCESS)
    {
        // Check to make sure we have the needed hardware support
        uint32_t i = 0;

        for (i=0; i<num_hw_watchpoints; ++i)
        {
            if ((m_state.dbg.__wcr[i] & WCR_ENABLE) == 0)
                break; // We found an available hw watchpoint slot (in i)
        }

        // See if we found an available hw watchpoint slot above
        if (i < num_hw_watchpoints)
        {
            //DumpDBGState(m_state.dbg);

            // Clear any previous LoHi joined-watchpoint that may have been in use
            LoHi[i] = 0;

            // shift our Byte Address Select bits up to the correct bit range for the DBGWCRn_EL1
            byte_address_select = byte_address_select << 5;
    
            // Make sure bits 1:0 are clear in our address
            m_state.dbg.__wvr[i] = aligned_wp_address;          // DVA (Data Virtual Address)
            m_state.dbg.__wcr[i] =  byte_address_select |       // Which bytes that follow the DVA that we will watch
                                    S_USER |                    // Stop only in user mode
                                    (read ? WCR_LOAD : 0) |     // Stop on read access?
                                    (write ? WCR_STORE : 0) |   // Stop on write access?
                                    WCR_ENABLE;                 // Enable this watchpoint;

            DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::EnableHardwareWatchpoint() adding watchpoint on address 0x%llx with control register value 0x%x", (uint64_t) m_state.dbg.__wvr[i], (uint32_t) m_state.dbg.__wcr[i]);

            // The kernel will set the MDE_ENABLE bit in the MDSCR_EL1 for us automatically, don't need to do it here.

            kret = SetDBGState(also_set_on_task);
            //DumpDBGState(m_state.dbg);

            DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::EnableHardwareWatchpoint() SetDBGState() => 0x%8.8x.", kret);

            if (kret == KERN_SUCCESS)
                return i;
        }
        else
        {
            DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::EnableHardwareWatchpoint(): All hardware resources (%u) are in use.", num_hw_watchpoints);
        }
    }
    return INVALID_NUB_HW_INDEX;
}

bool
DNBArchMachARM64::ReenableHardwareWatchpoint (uint32_t hw_index)
{
    // If this logical watchpoint # is actually implemented using
    // two hardware watchpoint registers, re-enable both of them.

    if (hw_index < NumSupportedHardwareWatchpoints() && LoHi[hw_index])
    {
        return ReenableHardwareWatchpoint_helper (hw_index) && ReenableHardwareWatchpoint_helper (LoHi[hw_index]);
    }
    else
    {
        return ReenableHardwareWatchpoint_helper (hw_index);
    }
}

bool
DNBArchMachARM64::ReenableHardwareWatchpoint_helper (uint32_t hw_index)
{
    kern_return_t kret = GetDBGState(false);
    if (kret != KERN_SUCCESS)
        return false;

    const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
    if (hw_index >= num_hw_points)
        return false;

    m_state.dbg.__wvr[hw_index] = m_disabled_watchpoints[hw_index].addr;
    m_state.dbg.__wcr[hw_index] = m_disabled_watchpoints[hw_index].control;

    DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::EnableHardwareWatchpoint( %u ) - WVR%u = 0x%8.8llx  WCR%u = 0x%8.8llx",
                     hw_index,
                     hw_index,
                     (uint64_t) m_state.dbg.__wvr[hw_index],
                     hw_index,
                     (uint64_t) m_state.dbg.__wcr[hw_index]);

   // The kernel will set the MDE_ENABLE bit in the MDSCR_EL1 for us automatically, don't need to do it here.

    kret = SetDBGState(false);

    return (kret == KERN_SUCCESS);
}

bool
DNBArchMachARM64::DisableHardwareWatchpoint (uint32_t hw_index, bool also_set_on_task)
{
    if (hw_index < NumSupportedHardwareWatchpoints() && LoHi[hw_index])
    {
        return DisableHardwareWatchpoint_helper (hw_index, also_set_on_task) && DisableHardwareWatchpoint_helper (LoHi[hw_index], also_set_on_task);
    }
    else
    {
        return DisableHardwareWatchpoint_helper (hw_index, also_set_on_task);
    }
}

bool
DNBArchMachARM64::DisableHardwareWatchpoint_helper (uint32_t hw_index, bool also_set_on_task)
{
    kern_return_t kret = GetDBGState(false);
    if (kret != KERN_SUCCESS)
        return false;

    const uint32_t num_hw_points = NumSupportedHardwareWatchpoints();
    if (hw_index >= num_hw_points)
        return false;

    m_disabled_watchpoints[hw_index].addr = m_state.dbg.__wvr[hw_index];
    m_disabled_watchpoints[hw_index].control = m_state.dbg.__wcr[hw_index];

    m_state.dbg.__wcr[hw_index] &= ~((nub_addr_t)WCR_ENABLE);
    DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::DisableHardwareWatchpoint( %u ) - WVR%u = 0x%8.8llx  WCR%u = 0x%8.8llx",
                     hw_index,
                     hw_index,
                     (uint64_t) m_state.dbg.__wvr[hw_index],
                     hw_index,
                     (uint64_t) m_state.dbg.__wcr[hw_index]);

    kret = SetDBGState(also_set_on_task);

    return (kret == KERN_SUCCESS);
}

// This is for checking the Byte Address Select bits in the DBRWCRn_EL1 control register.
// Returns -1 if the trailing bit patterns are not one of:
// { 0b???????1, 0b??????10, 0b?????100, 0b????1000, 0b???10000, 0b??100000, 0b?1000000, 0b10000000 }.
static inline
int32_t
LowestBitSet(uint32_t val)
{
    for (unsigned i = 0; i < 8; ++i) {
        if (bit(val, i))
            return i;
    }
    return -1;
}

// Iterate through the debug registers; return the index of the first watchpoint whose address matches.
// As a side effect, the starting address as understood by the debugger is returned which could be
// different from 'addr' passed as an in/out argument.
uint32_t
DNBArchMachARM64::GetHardwareWatchpointHit(nub_addr_t &addr)
{
    // Read the debug state
    kern_return_t kret = GetDBGState(true);
    //DumpDBGState(m_state.dbg);
    DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::GetHardwareWatchpointHit() GetDBGState() => 0x%8.8x.", kret);
    DNBLogThreadedIf(LOG_WATCHPOINTS, "DNBArchMachARM64::GetHardwareWatchpointHit() addr = 0x%llx", (uint64_t)addr);

    // This is the watchpoint value to match against, i.e., word address.
    nub_addr_t wp_val = addr & ~((nub_addr_t)3);
    if (kret == KERN_SUCCESS)
    {
        DBG &debug_state = m_state.dbg;
        uint32_t i, num = NumSupportedHardwareWatchpoints();
        for (i = 0; i < num; ++i)
        {
            nub_addr_t wp_addr = GetWatchAddress(debug_state, i);
            DNBLogThreadedIf(LOG_WATCHPOINTS,
                             "DNBArchMachARM64::GetHardwareWatchpointHit() slot: %u (addr = 0x%llx).",
                             i, (uint64_t)wp_addr);
            if (wp_val == wp_addr) {
                uint32_t byte_mask = bits(debug_state.__wcr[i], 12, 5);

                // Sanity check the byte_mask, first.
                if (LowestBitSet(byte_mask) < 0)
                    continue;

                // Check that the watchpoint is enabled.
                if (!IsWatchpointEnabled(debug_state, i))
                    continue;
    
                // Compute the starting address (from the point of view of the debugger).
                addr = wp_addr + LowestBitSet(byte_mask);
                return i;
            }
        }
    }
    return INVALID_NUB_HW_INDEX;
}

nub_addr_t
DNBArchMachARM64::GetWatchpointAddressByIndex (uint32_t hw_index)
{
    kern_return_t kret = GetDBGState(true);
    if (kret != KERN_SUCCESS)
        return INVALID_NUB_ADDRESS;
    const uint32_t num = NumSupportedHardwareWatchpoints();
    if (hw_index >= num)
        return INVALID_NUB_ADDRESS;
    if (IsWatchpointEnabled (m_state.dbg, hw_index))
        return GetWatchAddress (m_state.dbg, hw_index);
    return INVALID_NUB_ADDRESS;
}

bool
DNBArchMachARM64::IsWatchpointEnabled(const DBG &debug_state, uint32_t hw_index)
{
    // Watchpoint Control Registers, bitfield definitions
    // ...
    // Bits    Value    Description
    // [0]     0        Watchpoint disabled
    //         1        Watchpoint enabled.
    return (debug_state.__wcr[hw_index] & 1u);
}

nub_addr_t
DNBArchMachARM64::GetWatchAddress(const DBG &debug_state, uint32_t hw_index)
{
    // Watchpoint Value Registers, bitfield definitions
    // Bits        Description
    // [31:2]      Watchpoint value (word address, i.e., 4-byte aligned)
    // [1:0]       RAZ/SBZP
    return bits(debug_state.__wvr[hw_index], 63, 0);
}

//----------------------------------------------------------------------
// Register information defintions for 64 bit ARMv8.
//----------------------------------------------------------------------
enum gpr_regnums
{
    gpr_x0 = 0,
    gpr_x1,
    gpr_x2,
    gpr_x3,
    gpr_x4,
    gpr_x5,
    gpr_x6,
    gpr_x7,
    gpr_x8,
    gpr_x9,
    gpr_x10,
    gpr_x11,
    gpr_x12,
    gpr_x13,
    gpr_x14,
    gpr_x15,
    gpr_x16,
    gpr_x17,
    gpr_x18,
    gpr_x19,
    gpr_x20,
    gpr_x21,
    gpr_x22,
    gpr_x23,
    gpr_x24,
    gpr_x25,
    gpr_x26,
    gpr_x27,
    gpr_x28,
    gpr_fp, gpr_x29 = gpr_fp,
    gpr_lr,	gpr_x30 = gpr_lr,
    gpr_sp,	gpr_x31 = gpr_sp,
    gpr_pc,
    gpr_cpsr,

    gpr_w0 = 132,   // the number 132 is used to match the w0 register number in RNBRemote.cpp
    gpr_w1,
    gpr_w2,
    gpr_w3,
    gpr_w4,
    gpr_w5,
    gpr_w6,
    gpr_w7,
    gpr_w8,
    gpr_w9,
    gpr_w10,
    gpr_w11,
    gpr_w12,
    gpr_w13,
    gpr_w14,
    gpr_w15,
    gpr_w16,
    gpr_w17,
    gpr_w18,
    gpr_w19,
    gpr_w20,
    gpr_w21,
    gpr_w22,
    gpr_w23,
    gpr_w24,
    gpr_w25,
    gpr_w26,
    gpr_w27,
    gpr_w28

};

enum 
{
    vfp_v0 = 34, // the number 34 is to match the v0 register number in RNBRemote.cpp
    vfp_v1,
    vfp_v2,
    vfp_v3,
    vfp_v4,
    vfp_v5,
    vfp_v6,
    vfp_v7,
    vfp_v8,
    vfp_v9,
    vfp_v10,
    vfp_v11,
    vfp_v12,
    vfp_v13,
    vfp_v14,
    vfp_v15,
    vfp_v16,
    vfp_v17,
    vfp_v18,
    vfp_v19,
    vfp_v20,
    vfp_v21,
    vfp_v22,
    vfp_v23,
    vfp_v24,
    vfp_v25,
    vfp_v26,
    vfp_v27,
    vfp_v28,
    vfp_v29,
    vfp_v30,
    vfp_v31,
    vfp_fpsr,
    vfp_fpcr,

    // lower 32 bits of the corresponding vfp_v<n> reg.
    vfp_s0 = 68, // the number 68 is to match the s0 register number in RNBRemote.cpp
    vfp_s1,
    vfp_s2,
    vfp_s3,
    vfp_s4,
    vfp_s5,
    vfp_s6,
    vfp_s7,
    vfp_s8,
    vfp_s9,
    vfp_s10,
    vfp_s11,
    vfp_s12,
    vfp_s13,
    vfp_s14,
    vfp_s15,
    vfp_s16,
    vfp_s17,
    vfp_s18,
    vfp_s19,
    vfp_s20,
    vfp_s21,
    vfp_s22,
    vfp_s23,
    vfp_s24,
    vfp_s25,
    vfp_s26,
    vfp_s27,
    vfp_s28,
    vfp_s29,
    vfp_s30,
    vfp_s31,

    // lower 64 bits of the corresponding vfp_v<n> reg.
    vfp_d0 = 100,    // the number 100 is to match the d0 register number in RNBRemote.cpp
    vfp_d1,
    vfp_d2,
    vfp_d3,
    vfp_d4,
    vfp_d5,
    vfp_d6,
    vfp_d7,
    vfp_d8,
    vfp_d9,
    vfp_d10,
    vfp_d11,
    vfp_d12,
    vfp_d13,
    vfp_d14,
    vfp_d15,
    vfp_d16,
    vfp_d17,
    vfp_d18,
    vfp_d19,
    vfp_d20,
    vfp_d21,
    vfp_d22,
    vfp_d23,
    vfp_d24,
    vfp_d25,
    vfp_d26,
    vfp_d27,
    vfp_d28,
    vfp_d29,
    vfp_d30,
    vfp_d31
};

enum 
{
    exc_far = 0,
    exc_esr,
    exc_exception
};

// These numbers from the "DWARF for the ARM 64-bit Architecture (AArch64)" document.

enum
{
    dwarf_x0 = 0,
    dwarf_x1,
    dwarf_x2,
    dwarf_x3,
    dwarf_x4,
    dwarf_x5,
    dwarf_x6,
    dwarf_x7,
    dwarf_x8,
    dwarf_x9,
    dwarf_x10,
    dwarf_x11,
    dwarf_x12,
    dwarf_x13,
    dwarf_x14,
    dwarf_x15,
    dwarf_x16,
    dwarf_x17,
    dwarf_x18,
    dwarf_x19,
    dwarf_x20,
    dwarf_x21,
    dwarf_x22,
    dwarf_x23,
    dwarf_x24,
    dwarf_x25,
    dwarf_x26,
    dwarf_x27,
    dwarf_x28,
    dwarf_x29,
    dwarf_x30,   
    dwarf_x31,
    dwarf_pc        = 32,
    dwarf_elr_mode  = 33,
    dwarf_fp = dwarf_x29,
    dwarf_lr = dwarf_x30,
    dwarf_sp = dwarf_x31,
    // 34-63 reserved
    
    // V0-V31 (128 bit vector registers)
    dwarf_v0        = 64,
    dwarf_v1,
    dwarf_v2,
    dwarf_v3,
    dwarf_v4,
    dwarf_v5,
    dwarf_v6,
    dwarf_v7,
    dwarf_v8,
    dwarf_v9,
    dwarf_v10,
    dwarf_v11,
    dwarf_v12,
    dwarf_v13,
    dwarf_v14,
    dwarf_v15,
    dwarf_v16,
    dwarf_v17,
    dwarf_v18,
    dwarf_v19,
    dwarf_v20,
    dwarf_v21,
    dwarf_v22,
    dwarf_v23,
    dwarf_v24,
    dwarf_v25,
    dwarf_v26,
    dwarf_v27,
    dwarf_v28,
    dwarf_v29,
    dwarf_v30,
    dwarf_v31
    
    // 96-127 reserved
};

enum 
{
    gdb_gpr_x0 = 0,
    gdb_gpr_x1,
    gdb_gpr_x2,
    gdb_gpr_x3,
    gdb_gpr_x4,
    gdb_gpr_x5,
    gdb_gpr_x6,
    gdb_gpr_x7,
    gdb_gpr_x8,
    gdb_gpr_x9,
    gdb_gpr_x10,
    gdb_gpr_x11,
    gdb_gpr_x12,
    gdb_gpr_x13,
    gdb_gpr_x14,
    gdb_gpr_x15,
    gdb_gpr_x16,
    gdb_gpr_x17,
    gdb_gpr_x18,
    gdb_gpr_x19,
    gdb_gpr_x20,
    gdb_gpr_x21,
    gdb_gpr_x22,
    gdb_gpr_x23,
    gdb_gpr_x24,
    gdb_gpr_x25,
    gdb_gpr_x26,
    gdb_gpr_x27,
    gdb_gpr_x28,
    gdb_gpr_fp,    // x29
    gdb_gpr_lr,    // x30
    gdb_gpr_sp,    // sp aka xsp
    gdb_gpr_pc,
    gdb_gpr_cpsr,
    gdb_vfp_v0,
    gdb_vfp_v1,
    gdb_vfp_v2,
    gdb_vfp_v3,
    gdb_vfp_v4,
    gdb_vfp_v5,
    gdb_vfp_v6,
    gdb_vfp_v7,
    gdb_vfp_v8,
    gdb_vfp_v9,
    gdb_vfp_v10,
    gdb_vfp_v11,
    gdb_vfp_v12,
    gdb_vfp_v13,
    gdb_vfp_v14,
    gdb_vfp_v15,
    gdb_vfp_v16,
    gdb_vfp_v17,
    gdb_vfp_v18,
    gdb_vfp_v19,
    gdb_vfp_v20,
    gdb_vfp_v21,
    gdb_vfp_v22,
    gdb_vfp_v23,
    gdb_vfp_v24,
    gdb_vfp_v25,
    gdb_vfp_v26,
    gdb_vfp_v27,
    gdb_vfp_v28,
    gdb_vfp_v29,
    gdb_vfp_v30,
    gdb_vfp_v31,
    gdb_vfp_fpsr,
    gdb_vfp_fpcr
};

uint32_t g_contained_x0[] {gpr_x0, INVALID_NUB_REGNUM };
uint32_t g_contained_x1[] {gpr_x1, INVALID_NUB_REGNUM };
uint32_t g_contained_x2[] {gpr_x2, INVALID_NUB_REGNUM };
uint32_t g_contained_x3[] {gpr_x3, INVALID_NUB_REGNUM };
uint32_t g_contained_x4[] {gpr_x4, INVALID_NUB_REGNUM };
uint32_t g_contained_x5[] {gpr_x5, INVALID_NUB_REGNUM };
uint32_t g_contained_x6[] {gpr_x6, INVALID_NUB_REGNUM };
uint32_t g_contained_x7[] {gpr_x7, INVALID_NUB_REGNUM };
uint32_t g_contained_x8[] {gpr_x8, INVALID_NUB_REGNUM };
uint32_t g_contained_x9[] {gpr_x9, INVALID_NUB_REGNUM };
uint32_t g_contained_x10[] {gpr_x10, INVALID_NUB_REGNUM };
uint32_t g_contained_x11[] {gpr_x11, INVALID_NUB_REGNUM };
uint32_t g_contained_x12[] {gpr_x12, INVALID_NUB_REGNUM };
uint32_t g_contained_x13[] {gpr_x13, INVALID_NUB_REGNUM };
uint32_t g_contained_x14[] {gpr_x14, INVALID_NUB_REGNUM };
uint32_t g_contained_x15[] {gpr_x15, INVALID_NUB_REGNUM };
uint32_t g_contained_x16[] {gpr_x16, INVALID_NUB_REGNUM };
uint32_t g_contained_x17[] {gpr_x17, INVALID_NUB_REGNUM };
uint32_t g_contained_x18[] {gpr_x18, INVALID_NUB_REGNUM };
uint32_t g_contained_x19[] {gpr_x19, INVALID_NUB_REGNUM };
uint32_t g_contained_x20[] {gpr_x20, INVALID_NUB_REGNUM };
uint32_t g_contained_x21[] {gpr_x21, INVALID_NUB_REGNUM };
uint32_t g_contained_x22[] {gpr_x22, INVALID_NUB_REGNUM };
uint32_t g_contained_x23[] {gpr_x23, INVALID_NUB_REGNUM };
uint32_t g_contained_x24[] {gpr_x24, INVALID_NUB_REGNUM };
uint32_t g_contained_x25[] {gpr_x25, INVALID_NUB_REGNUM };
uint32_t g_contained_x26[] {gpr_x26, INVALID_NUB_REGNUM };
uint32_t g_contained_x27[] {gpr_x27, INVALID_NUB_REGNUM };
uint32_t g_contained_x28[] {gpr_x28, INVALID_NUB_REGNUM };

uint32_t g_invalidate_x0[] {gpr_x0, gpr_w0, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x1[] {gpr_x1, gpr_w1, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x2[] {gpr_x2, gpr_w2, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x3[] {gpr_x3, gpr_w3, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x4[] {gpr_x4, gpr_w4, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x5[] {gpr_x5, gpr_w5, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x6[] {gpr_x6, gpr_w6, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x7[] {gpr_x7, gpr_w7, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x8[] {gpr_x8, gpr_w8, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x9[] {gpr_x9, gpr_w9, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x10[] {gpr_x10, gpr_w10, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x11[] {gpr_x11, gpr_w11, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x12[] {gpr_x12, gpr_w12, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x13[] {gpr_x13, gpr_w13, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x14[] {gpr_x14, gpr_w14, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x15[] {gpr_x15, gpr_w15, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x16[] {gpr_x16, gpr_w16, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x17[] {gpr_x17, gpr_w17, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x18[] {gpr_x18, gpr_w18, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x19[] {gpr_x19, gpr_w19, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x20[] {gpr_x20, gpr_w20, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x21[] {gpr_x21, gpr_w21, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x22[] {gpr_x22, gpr_w22, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x23[] {gpr_x23, gpr_w23, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x24[] {gpr_x24, gpr_w24, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x25[] {gpr_x25, gpr_w25, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x26[] {gpr_x26, gpr_w26, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x27[] {gpr_x27, gpr_w27, INVALID_NUB_REGNUM };
uint32_t g_invalidate_x28[] {gpr_x28, gpr_w28, INVALID_NUB_REGNUM };

#define GPR_OFFSET_IDX(idx) (offsetof (DNBArchMachARM64::GPR, __x[idx]))

#define GPR_OFFSET_NAME(reg) (offsetof (DNBArchMachARM64::GPR , __##reg))

// These macros will auto define the register name, alt name, register size,
// register offset, encoding, format and native register. This ensures that
// the register state structures are defined correctly and have the correct
// sizes and offsets.
#define DEFINE_GPR_IDX(idx, reg, alt, gen) { e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, 8, GPR_OFFSET_IDX(idx) , dwarf_##reg, dwarf_##reg, gen, gdb_gpr_##reg, NULL, g_invalidate_x##idx }
#define DEFINE_GPR_NAME(reg, alt, gen)     { e_regSetGPR, gpr_##reg, #reg, alt, Uint, Hex, 8, GPR_OFFSET_NAME(reg), dwarf_##reg, dwarf_##reg, gen, gdb_gpr_##reg, NULL, NULL }
#define DEFINE_PSEUDO_GPR_IDX(idx, reg) { e_regSetGPR, gpr_##reg, #reg, NULL, Uint, Hex, 8, GPR_OFFSET_IDX(idx) , INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, g_contained_x##idx, g_invalidate_x##idx }

//_STRUCT_ARM_THREAD_STATE64
//{
//	uint64_t    x[29];	/* General purpose registers x0-x28 */
//	uint64_t    fp;		/* Frame pointer x29 */
//	uint64_t    lr;		/* Link register x30 */
//	uint64_t    sp;		/* Stack pointer x31 */
//	uint64_t    pc;		/* Program counter */
//	uint32_t    cpsr;	/* Current program status register */
//};


// General purpose registers
const DNBRegisterInfo
DNBArchMachARM64::g_gpr_registers[] =
{
    DEFINE_GPR_IDX ( 0,  x0, "arg1", GENERIC_REGNUM_ARG1  ),
    DEFINE_GPR_IDX ( 1,  x1, "arg2", GENERIC_REGNUM_ARG2  ),
    DEFINE_GPR_IDX ( 2,  x2, "arg3", GENERIC_REGNUM_ARG3  ),
    DEFINE_GPR_IDX ( 3,  x3, "arg4", GENERIC_REGNUM_ARG4  ),
    DEFINE_GPR_IDX ( 4,  x4, "arg5", GENERIC_REGNUM_ARG5  ),
    DEFINE_GPR_IDX ( 5,  x5, "arg6", GENERIC_REGNUM_ARG6  ),
    DEFINE_GPR_IDX ( 6,  x6,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX ( 7,  x7,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX ( 8,  x8,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX ( 9,  x9,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (10, x10,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (11, x11,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (12, x12,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (13, x13,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (14, x14,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (15, x15,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (16, x16,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (17, x17,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (18, x18,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (19, x19,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (20, x20,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (21, x21,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (22, x22,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (23, x23,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (24, x24,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (25, x25,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (26, x26,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (27, x27,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_IDX (28, x28,   NULL, INVALID_NUB_REGNUM   ),
    DEFINE_GPR_NAME (fp, "x29", GENERIC_REGNUM_FP),
    DEFINE_GPR_NAME (lr, "x30", GENERIC_REGNUM_RA),
    DEFINE_GPR_NAME (sp, "xsp",  GENERIC_REGNUM_SP),
    DEFINE_GPR_NAME (pc,  NULL, GENERIC_REGNUM_PC),

    // in armv7 we specify that writing to the CPSR should invalidate r8-12, sp, lr.
    // this should be spcified for arm64 too even though debugserver is only used for
    // userland debugging.
    { e_regSetGPR, gpr_cpsr, "cpsr", "flags", Uint, Hex, 4, GPR_OFFSET_NAME(cpsr), dwarf_elr_mode, dwarf_elr_mode, INVALID_NUB_REGNUM, gdb_gpr_cpsr, NULL, NULL },

    DEFINE_PSEUDO_GPR_IDX ( 0,  w0), 
    DEFINE_PSEUDO_GPR_IDX ( 1,  w1), 
    DEFINE_PSEUDO_GPR_IDX ( 2,  w2), 
    DEFINE_PSEUDO_GPR_IDX ( 3,  w3), 
    DEFINE_PSEUDO_GPR_IDX ( 4,  w4), 
    DEFINE_PSEUDO_GPR_IDX ( 5,  w5), 
    DEFINE_PSEUDO_GPR_IDX ( 6,  w6),
    DEFINE_PSEUDO_GPR_IDX ( 7,  w7),
    DEFINE_PSEUDO_GPR_IDX ( 8,  w8),
    DEFINE_PSEUDO_GPR_IDX ( 9,  w9),
    DEFINE_PSEUDO_GPR_IDX (10, w10),
    DEFINE_PSEUDO_GPR_IDX (11, w11),
    DEFINE_PSEUDO_GPR_IDX (12, w12),
    DEFINE_PSEUDO_GPR_IDX (13, w13),
    DEFINE_PSEUDO_GPR_IDX (14, w14),
    DEFINE_PSEUDO_GPR_IDX (15, w15),
    DEFINE_PSEUDO_GPR_IDX (16, w16),
    DEFINE_PSEUDO_GPR_IDX (17, w17),
    DEFINE_PSEUDO_GPR_IDX (18, w18),
    DEFINE_PSEUDO_GPR_IDX (19, w19),
    DEFINE_PSEUDO_GPR_IDX (20, w20),
    DEFINE_PSEUDO_GPR_IDX (21, w21),
    DEFINE_PSEUDO_GPR_IDX (22, w22),
    DEFINE_PSEUDO_GPR_IDX (23, w23),
    DEFINE_PSEUDO_GPR_IDX (24, w24),
    DEFINE_PSEUDO_GPR_IDX (25, w25),
    DEFINE_PSEUDO_GPR_IDX (26, w26),
    DEFINE_PSEUDO_GPR_IDX (27, w27),
    DEFINE_PSEUDO_GPR_IDX (28, w28)
};

uint32_t g_contained_v0[] {vfp_v0, INVALID_NUB_REGNUM };
uint32_t g_contained_v1[] {vfp_v1, INVALID_NUB_REGNUM };
uint32_t g_contained_v2[] {vfp_v2, INVALID_NUB_REGNUM };
uint32_t g_contained_v3[] {vfp_v3, INVALID_NUB_REGNUM };
uint32_t g_contained_v4[] {vfp_v4, INVALID_NUB_REGNUM };
uint32_t g_contained_v5[] {vfp_v5, INVALID_NUB_REGNUM };
uint32_t g_contained_v6[] {vfp_v6, INVALID_NUB_REGNUM };
uint32_t g_contained_v7[] {vfp_v7, INVALID_NUB_REGNUM };
uint32_t g_contained_v8[] {vfp_v8, INVALID_NUB_REGNUM };
uint32_t g_contained_v9[] {vfp_v9, INVALID_NUB_REGNUM };
uint32_t g_contained_v10[] {vfp_v10, INVALID_NUB_REGNUM };
uint32_t g_contained_v11[] {vfp_v11, INVALID_NUB_REGNUM };
uint32_t g_contained_v12[] {vfp_v12, INVALID_NUB_REGNUM };
uint32_t g_contained_v13[] {vfp_v13, INVALID_NUB_REGNUM };
uint32_t g_contained_v14[] {vfp_v14, INVALID_NUB_REGNUM };
uint32_t g_contained_v15[] {vfp_v15, INVALID_NUB_REGNUM };
uint32_t g_contained_v16[] {vfp_v16, INVALID_NUB_REGNUM };
uint32_t g_contained_v17[] {vfp_v17, INVALID_NUB_REGNUM };
uint32_t g_contained_v18[] {vfp_v18, INVALID_NUB_REGNUM };
uint32_t g_contained_v19[] {vfp_v19, INVALID_NUB_REGNUM };
uint32_t g_contained_v20[] {vfp_v20, INVALID_NUB_REGNUM };
uint32_t g_contained_v21[] {vfp_v21, INVALID_NUB_REGNUM };
uint32_t g_contained_v22[] {vfp_v22, INVALID_NUB_REGNUM };
uint32_t g_contained_v23[] {vfp_v23, INVALID_NUB_REGNUM };
uint32_t g_contained_v24[] {vfp_v24, INVALID_NUB_REGNUM };
uint32_t g_contained_v25[] {vfp_v25, INVALID_NUB_REGNUM };
uint32_t g_contained_v26[] {vfp_v26, INVALID_NUB_REGNUM };
uint32_t g_contained_v27[] {vfp_v27, INVALID_NUB_REGNUM };
uint32_t g_contained_v28[] {vfp_v28, INVALID_NUB_REGNUM };
uint32_t g_contained_v29[] {vfp_v29, INVALID_NUB_REGNUM };
uint32_t g_contained_v30[] {vfp_v30, INVALID_NUB_REGNUM };
uint32_t g_contained_v31[] {vfp_v31, INVALID_NUB_REGNUM };

uint32_t g_invalidate_v0[] {vfp_v0, vfp_d0, vfp_s0, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v1[] {vfp_v1, vfp_d1, vfp_s1, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v2[] {vfp_v2, vfp_d2, vfp_s2, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v3[] {vfp_v3, vfp_d3, vfp_s3, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v4[] {vfp_v4, vfp_d4, vfp_s4, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v5[] {vfp_v5, vfp_d5, vfp_s5, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v6[] {vfp_v6, vfp_d6, vfp_s6, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v7[] {vfp_v7, vfp_d7, vfp_s7, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v8[] {vfp_v8, vfp_d8, vfp_s8, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v9[] {vfp_v9, vfp_d9, vfp_s9, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v10[] {vfp_v10, vfp_d10, vfp_s10, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v11[] {vfp_v11, vfp_d11, vfp_s11, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v12[] {vfp_v12, vfp_d12, vfp_s12, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v13[] {vfp_v13, vfp_d13, vfp_s13, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v14[] {vfp_v14, vfp_d14, vfp_s14, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v15[] {vfp_v15, vfp_d15, vfp_s15, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v16[] {vfp_v16, vfp_d16, vfp_s16, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v17[] {vfp_v17, vfp_d17, vfp_s17, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v18[] {vfp_v18, vfp_d18, vfp_s18, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v19[] {vfp_v19, vfp_d19, vfp_s19, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v20[] {vfp_v20, vfp_d20, vfp_s20, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v21[] {vfp_v21, vfp_d21, vfp_s21, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v22[] {vfp_v22, vfp_d22, vfp_s22, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v23[] {vfp_v23, vfp_d23, vfp_s23, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v24[] {vfp_v24, vfp_d24, vfp_s24, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v25[] {vfp_v25, vfp_d25, vfp_s25, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v26[] {vfp_v26, vfp_d26, vfp_s26, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v27[] {vfp_v27, vfp_d27, vfp_s27, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v28[] {vfp_v28, vfp_d28, vfp_s28, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v29[] {vfp_v29, vfp_d29, vfp_s29, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v30[] {vfp_v30, vfp_d30, vfp_s30, INVALID_NUB_REGNUM };
uint32_t g_invalidate_v31[] {vfp_v31, vfp_d31, vfp_s31, INVALID_NUB_REGNUM };

#if defined (__arm64__)
#define VFP_V_OFFSET_IDX(idx) (offsetof (DNBArchMachARM64::FPU, __v) + (idx * 16) + offsetof (DNBArchMachARM64::Context, vfp))
#else
#define VFP_V_OFFSET_IDX(idx) (offsetof (DNBArchMachARM64::FPU, opaque) + (idx * 16) + offsetof (DNBArchMachARM64::Context, vfp))
#endif
#define VFP_OFFSET_NAME(reg) (offsetof (DNBArchMachARM64::FPU, reg) + offsetof (DNBArchMachARM64::Context, vfp))
#define EXC_OFFSET(reg)      (offsetof (DNBArchMachARM64::EXC, reg)  + offsetof (DNBArchMachARM64::Context, exc))

//#define FLOAT_FORMAT Float
#define DEFINE_VFP_V_IDX(idx) { e_regSetVFP, vfp_v##idx - vfp_v0, "v" #idx, "q" #idx, Vector, VectorOfUInt8, 16, VFP_V_OFFSET_IDX(idx), INVALID_NUB_REGNUM, dwarf_v##idx, INVALID_NUB_REGNUM, gdb_vfp_v##idx, NULL, g_invalidate_v##idx }
#define DEFINE_PSEUDO_VFP_S_IDX(idx) { e_regSetVFP, vfp_s##idx - vfp_v0, "s" #idx, NULL, IEEE754, Float, 4, VFP_V_OFFSET_IDX(idx), INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, g_contained_v##idx, g_invalidate_v##idx }
#define DEFINE_PSEUDO_VFP_D_IDX(idx) { e_regSetVFP, vfp_d##idx - vfp_v0, "d" #idx, NULL, IEEE754, Float, 8, VFP_V_OFFSET_IDX(idx), INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, g_contained_v##idx, g_invalidate_v##idx }

//_STRUCT_ARM_VFP_STATE64
//{
//	uint128_t       v[32];
//	uint32_t        fpsr;
//	uint32_t        fpcr;
//};


// Floating point registers
const DNBRegisterInfo
DNBArchMachARM64::g_vfp_registers[] =
{
    DEFINE_VFP_V_IDX ( 0),
    DEFINE_VFP_V_IDX ( 1),
    DEFINE_VFP_V_IDX ( 2),
    DEFINE_VFP_V_IDX ( 3),
    DEFINE_VFP_V_IDX ( 4),
    DEFINE_VFP_V_IDX ( 5),
    DEFINE_VFP_V_IDX ( 6),
    DEFINE_VFP_V_IDX ( 7),
    DEFINE_VFP_V_IDX ( 8),
    DEFINE_VFP_V_IDX ( 9),
    DEFINE_VFP_V_IDX (10),
    DEFINE_VFP_V_IDX (11),
    DEFINE_VFP_V_IDX (12),
    DEFINE_VFP_V_IDX (13),
    DEFINE_VFP_V_IDX (14),
    DEFINE_VFP_V_IDX (15),
    DEFINE_VFP_V_IDX (16),
    DEFINE_VFP_V_IDX (17),
    DEFINE_VFP_V_IDX (18),
    DEFINE_VFP_V_IDX (19),
    DEFINE_VFP_V_IDX (20),
    DEFINE_VFP_V_IDX (21),
    DEFINE_VFP_V_IDX (22),
    DEFINE_VFP_V_IDX (23),
    DEFINE_VFP_V_IDX (24),
    DEFINE_VFP_V_IDX (25),
    DEFINE_VFP_V_IDX (26),
    DEFINE_VFP_V_IDX (27),
    DEFINE_VFP_V_IDX (28),
    DEFINE_VFP_V_IDX (29),
    DEFINE_VFP_V_IDX (30),
    DEFINE_VFP_V_IDX (31),
    { e_regSetVFP, vfp_fpsr, "fpsr", NULL, Uint, Hex, 4, 32 * 16 + 0, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL },
    { e_regSetVFP, vfp_fpcr, "fpcr", NULL, Uint, Hex, 4, 32 * 16 + 4, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL },

    DEFINE_PSEUDO_VFP_S_IDX (0),
    DEFINE_PSEUDO_VFP_S_IDX (1),
    DEFINE_PSEUDO_VFP_S_IDX (2),
    DEFINE_PSEUDO_VFP_S_IDX (3),
    DEFINE_PSEUDO_VFP_S_IDX (4),
    DEFINE_PSEUDO_VFP_S_IDX (5),
    DEFINE_PSEUDO_VFP_S_IDX (6),
    DEFINE_PSEUDO_VFP_S_IDX (7),
    DEFINE_PSEUDO_VFP_S_IDX (8),
    DEFINE_PSEUDO_VFP_S_IDX (9),
    DEFINE_PSEUDO_VFP_S_IDX (10),
    DEFINE_PSEUDO_VFP_S_IDX (11),
    DEFINE_PSEUDO_VFP_S_IDX (12),
    DEFINE_PSEUDO_VFP_S_IDX (13),
    DEFINE_PSEUDO_VFP_S_IDX (14),
    DEFINE_PSEUDO_VFP_S_IDX (15),
    DEFINE_PSEUDO_VFP_S_IDX (16),
    DEFINE_PSEUDO_VFP_S_IDX (17),
    DEFINE_PSEUDO_VFP_S_IDX (18),
    DEFINE_PSEUDO_VFP_S_IDX (19),
    DEFINE_PSEUDO_VFP_S_IDX (20),
    DEFINE_PSEUDO_VFP_S_IDX (21),
    DEFINE_PSEUDO_VFP_S_IDX (22),
    DEFINE_PSEUDO_VFP_S_IDX (23),
    DEFINE_PSEUDO_VFP_S_IDX (24),
    DEFINE_PSEUDO_VFP_S_IDX (25),
    DEFINE_PSEUDO_VFP_S_IDX (26),
    DEFINE_PSEUDO_VFP_S_IDX (27),
    DEFINE_PSEUDO_VFP_S_IDX (28),
    DEFINE_PSEUDO_VFP_S_IDX (29),
    DEFINE_PSEUDO_VFP_S_IDX (30),
    DEFINE_PSEUDO_VFP_S_IDX (31),

    DEFINE_PSEUDO_VFP_D_IDX (0),
    DEFINE_PSEUDO_VFP_D_IDX (1),
    DEFINE_PSEUDO_VFP_D_IDX (2),
    DEFINE_PSEUDO_VFP_D_IDX (3),
    DEFINE_PSEUDO_VFP_D_IDX (4),
    DEFINE_PSEUDO_VFP_D_IDX (5),
    DEFINE_PSEUDO_VFP_D_IDX (6),
    DEFINE_PSEUDO_VFP_D_IDX (7),
    DEFINE_PSEUDO_VFP_D_IDX (8),
    DEFINE_PSEUDO_VFP_D_IDX (9),
    DEFINE_PSEUDO_VFP_D_IDX (10),
    DEFINE_PSEUDO_VFP_D_IDX (11),
    DEFINE_PSEUDO_VFP_D_IDX (12),
    DEFINE_PSEUDO_VFP_D_IDX (13),
    DEFINE_PSEUDO_VFP_D_IDX (14),
    DEFINE_PSEUDO_VFP_D_IDX (15),
    DEFINE_PSEUDO_VFP_D_IDX (16),
    DEFINE_PSEUDO_VFP_D_IDX (17),
    DEFINE_PSEUDO_VFP_D_IDX (18),
    DEFINE_PSEUDO_VFP_D_IDX (19),
    DEFINE_PSEUDO_VFP_D_IDX (20),
    DEFINE_PSEUDO_VFP_D_IDX (21),
    DEFINE_PSEUDO_VFP_D_IDX (22),
    DEFINE_PSEUDO_VFP_D_IDX (23),
    DEFINE_PSEUDO_VFP_D_IDX (24),
    DEFINE_PSEUDO_VFP_D_IDX (25),
    DEFINE_PSEUDO_VFP_D_IDX (26),
    DEFINE_PSEUDO_VFP_D_IDX (27),
    DEFINE_PSEUDO_VFP_D_IDX (28),
    DEFINE_PSEUDO_VFP_D_IDX (29),
    DEFINE_PSEUDO_VFP_D_IDX (30),
    DEFINE_PSEUDO_VFP_D_IDX (31)

};


//_STRUCT_ARM_EXCEPTION_STATE64
//{
//	uint64_t	far; /* Virtual Fault Address */
//	uint32_t	esr; /* Exception syndrome */
//	uint32_t	exception; /* number of arm exception taken */
//};

// Exception registers
const DNBRegisterInfo
DNBArchMachARM64::g_exc_registers[] =
{
    { e_regSetEXC, exc_far        , "far"         , NULL, Uint, Hex, 8, EXC_OFFSET(__far)       , INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL },
    { e_regSetEXC, exc_esr        , "esr"         , NULL, Uint, Hex, 4, EXC_OFFSET(__esr)       , INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL },
    { e_regSetEXC, exc_exception  , "exception"   , NULL, Uint, Hex, 4, EXC_OFFSET(__exception) , INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, INVALID_NUB_REGNUM, NULL, NULL }
};

// Number of registers in each register set
const size_t DNBArchMachARM64::k_num_gpr_registers = sizeof(g_gpr_registers)/sizeof(DNBRegisterInfo);
const size_t DNBArchMachARM64::k_num_vfp_registers = sizeof(g_vfp_registers)/sizeof(DNBRegisterInfo);
const size_t DNBArchMachARM64::k_num_exc_registers = sizeof(g_exc_registers)/sizeof(DNBRegisterInfo);
const size_t DNBArchMachARM64::k_num_all_registers = k_num_gpr_registers + k_num_vfp_registers + k_num_exc_registers;

//----------------------------------------------------------------------
// Register set definitions. The first definitions at register set index
// of zero is for all registers, followed by other registers sets. The
// register information for the all register set need not be filled in.
//----------------------------------------------------------------------
const DNBRegisterSetInfo
DNBArchMachARM64::g_reg_sets[] =
{
    { "ARM64 Registers",            NULL,               k_num_all_registers     },
    { "General Purpose Registers",  g_gpr_registers,    k_num_gpr_registers     },
    { "Floating Point Registers",   g_vfp_registers,    k_num_vfp_registers     },
    { "Exception State Registers",  g_exc_registers,    k_num_exc_registers     }
};
// Total number of register sets for this architecture
const size_t DNBArchMachARM64::k_num_register_sets = sizeof(g_reg_sets)/sizeof(DNBRegisterSetInfo);


const DNBRegisterSetInfo *
DNBArchMachARM64::GetRegisterSetInfo(nub_size_t *num_reg_sets)
{
    *num_reg_sets = k_num_register_sets;
    return g_reg_sets;
}

bool
DNBArchMachARM64::FixGenericRegisterNumber (int &set, int &reg)
{
    if (set == REGISTER_SET_GENERIC)
    {
        switch (reg)
        {
            case GENERIC_REGNUM_PC:     // Program Counter
                set = e_regSetGPR;
                reg = gpr_pc;
                break;
                
            case GENERIC_REGNUM_SP:     // Stack Pointer
                set = e_regSetGPR;
                reg = gpr_sp;
                break;
                
            case GENERIC_REGNUM_FP:     // Frame Pointer
                set = e_regSetGPR;
                reg = gpr_fp;
                break;
                
            case GENERIC_REGNUM_RA:     // Return Address
                set = e_regSetGPR;
                reg = gpr_lr;
                break;
                
            case GENERIC_REGNUM_FLAGS:  // Processor flags register
                set = e_regSetGPR;
                reg = gpr_cpsr;
                break;
                
            case GENERIC_REGNUM_ARG1:
            case GENERIC_REGNUM_ARG2:
            case GENERIC_REGNUM_ARG3:
            case GENERIC_REGNUM_ARG4:
            case GENERIC_REGNUM_ARG5:
            case GENERIC_REGNUM_ARG6:
                set = e_regSetGPR;
                reg = gpr_x0 + reg - GENERIC_REGNUM_ARG1;
                break;
                
            default:
                return false;
        }
    }
    return true;
}
bool
DNBArchMachARM64::GetRegisterValue(int set, int reg, DNBRegisterValue *value)
{
    if (!FixGenericRegisterNumber (set, reg))
        return false;

    if (GetRegisterState(set, false) != KERN_SUCCESS)
        return false;

    const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
    if (regInfo)
    {
        value->info = *regInfo;
        switch (set)
        {
        case e_regSetGPR:
            if (reg <= gpr_pc)
            {
                value->value.uint64 = m_state.context.gpr.__x[reg];
                return true;
            }
            else if (reg == gpr_cpsr)
            {
                value->value.uint32 = m_state.context.gpr.__cpsr;
                return true;
            }
            break;

        case e_regSetVFP:
            reg += vfp_v0;

            if (reg >= vfp_v0 && reg <= vfp_v31)
            {
#if defined (__arm64__)
                memcpy (&value->value.v_uint8, ((uint8_t *) &m_state.context.vfp.__v) + ((reg - vfp_v0) * 16), 16);
#else
                memcpy (&value->value.v_uint8, ((uint8_t *) &m_state.context.vfp.opaque) + ((reg - vfp_v0) * 16), 16);
#endif
                return true;
            }
            else if (reg == vfp_fpsr)
            {
#if defined (__arm64__)
                memcpy (&value->value.uint32, ((uint8_t *) &m_state.context.vfp.__v) + (32 * 16) + 0, 4);
#else
                memcpy (&value->value.uint32, ((uint8_t *) &m_state.context.vfp.opaque) + (32 * 16) + 0, 4);
#endif
                return true;
            }
            else if (reg == vfp_fpcr)
            {
#if defined (__arm64__)
                memcpy (&value->value.uint32, ((uint8_t *) &m_state.context.vfp.__v) + (32 * 16) + 4, 4);
#else
                memcpy (&value->value.uint32, ((uint8_t *) &m_state.context.vfp.opaque) + (32 * 16) + 4, 4);
#endif
                return true;
            }
            else if (reg >= vfp_s0 && reg <= vfp_s31)
            {
#if defined (__arm64__)
                memcpy (&value->value.v_uint8, ((uint8_t *) &m_state.context.vfp.__v) + ((reg - vfp_s0) * 16), 4);
#else
                memcpy (&value->value.v_uint8, ((uint8_t *) &m_state.context.vfp.opaque) + ((reg - vfp_s0) * 16), 4);
#endif
                return true;
            }
            else if (reg >= vfp_d0 && reg <= vfp_d31)
            {
#if defined (__arm64__)
                memcpy (&value->value.v_uint8, ((uint8_t *) &m_state.context.vfp.__v) + ((reg - vfp_d0) * 16), 8);
#else
                memcpy (&value->value.v_uint8, ((uint8_t *) &m_state.context.vfp.opaque) + ((reg - vfp_d0) * 16), 8);
#endif
                return true;
            }
            break;

        case e_regSetEXC:
            if (reg == exc_far)
            {
                value->value.uint64 = m_state.context.exc.__far;
                return true;
            }
            else if (reg == exc_esr)
            {
                value->value.uint64 = m_state.context.exc.__esr;
                return true;
            }
            else if (reg == exc_exception)
            {
                value->value.uint32 = m_state.context.exc.__exception;
                return true;
            }
            break;
        }
    }
    return false;
}

bool
DNBArchMachARM64::SetRegisterValue(int set, int reg, const DNBRegisterValue *value)
{
    if (!FixGenericRegisterNumber (set, reg))
        return false;
 
    if (GetRegisterState(set, false) != KERN_SUCCESS)
        return false;

    bool success = false;
    const DNBRegisterInfo *regInfo = m_thread->GetRegisterInfo(set, reg);
    if (regInfo)
    {
        switch (set)
        {
        case e_regSetGPR:
            if (reg <= gpr_pc)
            {
                m_state.context.gpr.__x[reg] = value->value.uint64;
                success = true;
            }
            else if (reg == gpr_cpsr)
            {
                m_state.context.gpr.__cpsr = value->value.uint32;
                success = true;
            }
            break;
            
        case e_regSetVFP:
            reg += vfp_v0;

            if (reg >= vfp_v0 && reg <= vfp_v31)
            {
#if defined (__arm64__)
                memcpy (((uint8_t *) &m_state.context.vfp.__v) + ((reg - vfp_v0) * 16), &value->value.v_uint8, 16);
#else
                memcpy (((uint8_t *) &m_state.context.vfp.opaque) + ((reg - vfp_v0) * 16), &value->value.v_uint8, 16);
#endif
                success = true;
            }
            else if (reg == vfp_fpsr)
            {
#if defined (__arm64__)
                memcpy (((uint8_t *) &m_state.context.vfp.__v) + (32 * 16) + 0, &value->value.uint32, 4);
#else
                memcpy (((uint8_t *) &m_state.context.vfp.opaque) + (32 * 16) + 0, &value->value.uint32, 4);
#endif
                success = true;
            }
            else if (reg == vfp_fpcr)
            {
#if defined (__arm64__)
                memcpy (((uint8_t *) m_state.context.vfp.__v) + (32 * 16) + 4, &value->value.uint32, 4);
#else
                memcpy (((uint8_t *) m_state.context.vfp.opaque) + (32 * 16) + 4, &value->value.uint32, 4);
#endif
                success = true;
            }
            else if (reg >= vfp_s0 && reg <= vfp_s31)
            {
#if defined (__arm64__)
                memcpy (((uint8_t *) &m_state.context.vfp.__v) + ((reg - vfp_s0) * 16), &value->value.v_uint8, 4);
#else
                memcpy (((uint8_t *) &m_state.context.vfp.opaque) + ((reg - vfp_s0) * 16), &value->value.v_uint8, 4);
#endif
                success = true;
            }
            else if (reg >= vfp_d0 && reg <= vfp_d31)
            {
#if defined (__arm64__)
                memcpy (((uint8_t *) &m_state.context.vfp.__v) + ((reg - vfp_d0) * 16), &value->value.v_uint8, 8);
#else
                memcpy (((uint8_t *) &m_state.context.vfp.opaque) + ((reg - vfp_d0) * 16), &value->value.v_uint8, 8);
#endif
                success = true;
            }
            break;
            
        case e_regSetEXC:
            if (reg == exc_far)
            {
                m_state.context.exc.__far = value->value.uint64;
                success = true;
            }
            else if (reg == exc_esr)
            {
                m_state.context.exc.__esr = value->value.uint64;
                success = true;
            }
            else if (reg == exc_exception)
            {
                m_state.context.exc.__exception = value->value.uint32;
                success = true;
            }
            break;
        }

    }
    if (success)
        return SetRegisterState(set) == KERN_SUCCESS;
    return false;
}

kern_return_t
DNBArchMachARM64::GetRegisterState(int set, bool force)
{
    switch (set)
    {
    case e_regSetALL:   return GetGPRState(force) |
                               GetVFPState(force) |
                               GetEXCState(force) |
                               GetDBGState(force);
    case e_regSetGPR:   return GetGPRState(force);
    case e_regSetVFP:   return GetVFPState(force);
    case e_regSetEXC:   return GetEXCState(force);
    case e_regSetDBG:   return GetDBGState(force);
    default: break;
    }
    return KERN_INVALID_ARGUMENT;
}

kern_return_t
DNBArchMachARM64::SetRegisterState(int set)
{
    // Make sure we have a valid context to set.
    kern_return_t err = GetRegisterState(set, false);
    if (err != KERN_SUCCESS)
        return err;

    switch (set)
    {
    case e_regSetALL:   return SetGPRState() |
                               SetVFPState() |
                               SetEXCState() |
                               SetDBGState(false);
    case e_regSetGPR:   return SetGPRState();
    case e_regSetVFP:   return SetVFPState();
    case e_regSetEXC:   return SetEXCState();
    case e_regSetDBG:   return SetDBGState(false);
    default: break;
    }
    return KERN_INVALID_ARGUMENT;
}

bool
DNBArchMachARM64::RegisterSetStateIsValid (int set) const
{
    return m_state.RegsAreValid(set);
}


nub_size_t
DNBArchMachARM64::GetRegisterContext (void *buf, nub_size_t buf_len)
{
    nub_size_t size = sizeof (m_state.context);
    
    if (buf && buf_len)
    {
        if (size > buf_len)
            size = buf_len;

        bool force = false;
        if (GetGPRState(force) | GetVFPState(force) | GetEXCState(force))
            return 0;
        ::memcpy (buf, &m_state.context, size);
    }
    DNBLogThreadedIf (LOG_THREAD, "DNBArchMachARM64::GetRegisterContext (buf = %p, len = %zu) => %zu", buf, buf_len, size);
    // Return the size of the register context even if NULL was passed in
    return size;
}

nub_size_t
DNBArchMachARM64::SetRegisterContext (const void *buf, nub_size_t buf_len)
{
    nub_size_t size = sizeof (m_state.context);
    if (buf == NULL || buf_len == 0)
        size = 0;
    
    if (size)
    {
        if (size > buf_len)
            size = buf_len;

        ::memcpy (&m_state.context, buf, size);
        SetGPRState();
        SetVFPState();
        SetEXCState();
    }
    DNBLogThreadedIf (LOG_THREAD, "DNBArchMachARM64::SetRegisterContext (buf = %p, len = %zu) => %zu", buf, buf_len, size);
    return size;
}


#endif  // #if defined (ARM_THREAD_STATE64_COUNT)
#endif  // #if defined (__arm__)