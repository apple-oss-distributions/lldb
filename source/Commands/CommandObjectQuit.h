//===-- CommandObjectQuit.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandObjectQuit_h_
#define liblldb_CommandObjectQuit_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

//-------------------------------------------------------------------------
// CommandObjectQuit
//-------------------------------------------------------------------------

class CommandObjectQuit : public CommandObject
{
public:

    CommandObjectQuit (CommandInterpreter &interpreter);

    virtual
    ~CommandObjectQuit ();

    virtual bool
    Execute (Args& args,
             CommandReturnObject &result);

};

} // namespace lldb_private

#endif  // liblldb_CommandObjectQuit_h_