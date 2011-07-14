//===-- ThreadPlan.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlan.h"

// C Includes
#include <string.h>
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/ArchSpec.h"
#include "lldb/Core/DataBufferHeap.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/State.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb;
using namespace lldb_private;

#pragma mark ThreadPlanTracer

ThreadPlanTracer::ThreadPlanTracer (Thread &thread, lldb::StreamSP &stream_sp) :
    m_thread (thread),
    m_single_step(true),
    m_enabled (false),
    m_stream_sp (stream_sp)
{
}

ThreadPlanTracer::ThreadPlanTracer (Thread &thread) :
    m_thread (thread),
    m_single_step(true),
    m_enabled (false),
    m_stream_sp ()
{
}

Stream *
ThreadPlanTracer::GetLogStream ()
{
    
    if (m_stream_sp.get())
        return m_stream_sp.get();
    else
        return &m_thread.GetProcess().GetTarget().GetDebugger().GetOutputStream();
}

void 
ThreadPlanTracer::Log()
{
    SymbolContext sc;
    bool show_frame_index = false;
    bool show_fullpaths = false;
    
    Stream *stream = GetLogStream();
    m_thread.GetStackFrameAtIndex(0)->Dump (stream, show_frame_index, show_fullpaths);
    stream->Printf("\n");
    stream->Flush();
    
}

bool
ThreadPlanTracer::TracerExplainsStop ()
{
    if (m_enabled && m_single_step)
    {
        lldb::StopInfoSP stop_info = m_thread.GetStopInfo();
        if (stop_info->GetStopReason() == eStopReasonTrace)
            return true;
        else 
            return false;
    }
    else
        return false;
}

#pragma mark ThreadPlanAssemblyTracer

ThreadPlanAssemblyTracer::ThreadPlanAssemblyTracer (Thread &thread, lldb::StreamSP &stream_sp) :
    ThreadPlanTracer (thread, stream_sp),
    m_process(thread.GetProcess()),
    m_target(thread.GetProcess().GetTarget())
{
    InitializeTracer ();
}

ThreadPlanAssemblyTracer::ThreadPlanAssemblyTracer (Thread &thread) :
    ThreadPlanTracer (thread),
    m_process(thread.GetProcess()),
    m_target(thread.GetProcess().GetTarget())
{
    InitializeTracer ();
}

void
ThreadPlanAssemblyTracer::InitializeTracer()
{
    Process &process = m_thread.GetProcess();
    Target &target = process.GetTarget();
    
    ArchSpec arch(target.GetArchitecture());
    
    m_disassembler = Disassembler::FindPlugin(arch, NULL);
    
    m_abi = process.GetABI().get();
    
    ModuleSP exe_module_sp (target.GetExecutableModule());
    
    if (exe_module_sp)
    {
        m_intptr_type = TypeFromUser(exe_module_sp->GetClangASTContext().GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, arch.GetAddressByteSize() * 8),
                                     exe_module_sp->GetClangASTContext().getASTContext());
    }
    
    const unsigned int buf_size = 32;
    
    m_buffer_sp.reset(new DataBufferHeap(buf_size, 0));
}

ThreadPlanAssemblyTracer::~ThreadPlanAssemblyTracer()
{
}

void 
ThreadPlanAssemblyTracer::TracingStarted ()
{
    RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();
    
    if (m_register_values.size() == 0)
        m_register_values.resize (reg_ctx->GetRegisterCount());
}

void
ThreadPlanAssemblyTracer::TracingEnded ()
{
    m_register_values.clear();
}

static void
PadOutTo (StreamString &stream, int target)
{
    stream.Flush();

    int length = stream.GetString().length();

    if (length + 1 < target)
        stream.Printf("%*s", target - (length + 1) + 1, "");
}

void 
ThreadPlanAssemblyTracer::Log ()
{
    Stream *stream = GetLogStream ();
    
    if (!stream)
        return;
            
    RegisterContext *reg_ctx = m_thread.GetRegisterContext().get();
    
    lldb::addr_t pc = reg_ctx->GetPC();
    Address pc_addr;
    bool addr_valid = false;

    addr_valid = m_process.GetTarget().GetSectionLoadList().ResolveLoadAddress (pc, pc_addr);
    
    pc_addr.Dump(stream, &m_thread, Address::DumpStyleResolvedDescription, Address::DumpStyleModuleWithFileAddress);
    stream->PutCString (" ");
    
    if (m_disassembler)
    {        
        ::memset(m_buffer_sp->GetBytes(), 0, m_buffer_sp->GetByteSize());
        
        Error err;
        m_process.ReadMemory(pc, m_buffer_sp->GetBytes(), m_buffer_sp->GetByteSize(), err);
        
        if (err.Success())
        {
            DataExtractor extractor(m_buffer_sp,
                                    m_process.GetByteOrder(),
                                    m_process.GetAddressByteSize());
            
            if (addr_valid)
                m_disassembler->DecodeInstructions (pc_addr, extractor, 0, 1, false);
            else
                m_disassembler->DecodeInstructions (Address (NULL, pc), extractor, 0, 1, false);
            
            InstructionList &instruction_list = m_disassembler->GetInstructionList();
            const uint32_t max_opcode_byte_size = instruction_list.GetMaxOpcocdeByteSize();

            if (instruction_list.GetSize())
            {
                const bool show_bytes = true;
                const bool show_address = true;
                Instruction *instruction = instruction_list.GetInstructionAtIndex(0).get();
                instruction->Dump (stream,
                                   max_opcode_byte_size,
                                   show_address,
                                   show_bytes,
                                   NULL, 
                                   true);
            }
        }
    }
    
    if (m_abi && m_intptr_type.GetOpaqueQualType())
    {
        ValueList value_list;
        const int num_args = 1;
        
        for (int arg_index = 0; arg_index < num_args; ++arg_index)
        {
            Value value;
            value.SetValueType (Value::eValueTypeScalar);
            value.SetContext (Value::eContextTypeClangType, m_intptr_type.GetOpaqueQualType());
            value_list.PushValue (value);
        }
        
        if (m_abi->GetArgumentValues (m_thread, value_list))
        {                
            for (int arg_index = 0; arg_index < num_args; ++arg_index)
            {
                stream->Printf("\n\targ[%d]=%llx", arg_index, value_list.GetValueAtIndex(arg_index)->GetScalar().ULongLong());
                
                if (arg_index + 1 < num_args)
                    stream->PutCString (", ");
            }
        }
    }
    
    
    RegisterValue reg_value;
    for (uint32_t reg_num = 0, num_registers = reg_ctx->GetRegisterCount();
         reg_num < num_registers;
         ++reg_num)
    {
        const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoAtIndex(reg_num);
        if (reg_ctx->ReadRegister (reg_info, reg_value))
        {
            assert (reg_num < m_register_values.size());
            if (m_register_values[reg_num].GetType() == RegisterValue::eTypeInvalid || 
                reg_value != m_register_values[reg_num])
            {
                if (reg_value.GetType() != RegisterValue::eTypeInvalid)
                {
                    stream->PutCString ("\n\t");
                    reg_value.Dump(stream, reg_info, true, false, eFormatDefault);
                }
            }
            m_register_values[reg_num] = reg_value;
        }
    }
    stream->EOL();
    stream->Flush();
}