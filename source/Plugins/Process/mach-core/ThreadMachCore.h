//===-- ThreadMachCore.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ThreadMachCore_h_
#define liblldb_ThreadMachCore_h_

#include <string>

#include "lldb/Target/Thread.h"

class ProcessMachCore;

class ThreadMachCore : public lldb_private::Thread
{
public:
    ThreadMachCore (const lldb::ProcessSP &process_sp,
                    lldb::tid_t tid);

    virtual
    ~ThreadMachCore ();

    virtual void
    RefreshStateAfterStop();

    virtual const char *
    GetName ();

    virtual lldb::RegisterContextSP
    GetRegisterContext ();

    virtual lldb::RegisterContextSP
    CreateRegisterContextForFrame (lldb_private::StackFrame *frame);

    virtual void
    ClearStackFrames ();

    static bool
    ThreadIDIsValid (lldb::tid_t thread);

    bool
    ShouldStop (bool &step_more);

    const char *
    GetBasicInfoAsString ();

    void
    SetName (const char *name)
    {
        if (name && name[0])
            m_thread_name.assign (name);
        else
            m_thread_name.clear();
    }

    lldb::addr_t
    GetThreadDispatchQAddr ()
    {
        return m_thread_dispatch_qaddr;
    }

    void
    SetThreadDispatchQAddr (lldb::addr_t thread_dispatch_qaddr)
    {
        m_thread_dispatch_qaddr = thread_dispatch_qaddr;
    }

protected:
    
    friend class ProcessMachCore;

    //------------------------------------------------------------------
    // Member variables.
    //------------------------------------------------------------------
    std::string m_thread_name;
    std::string m_dispatch_queue_name;
    lldb::addr_t m_thread_dispatch_qaddr;
    lldb::RegisterContextSP m_thread_reg_ctx_sp;
    //------------------------------------------------------------------
    // Member variables.
    //------------------------------------------------------------------

    virtual lldb::StopInfoSP
    GetPrivateStopReason ();
};

#endif  // liblldb_ThreadMachCore_h_