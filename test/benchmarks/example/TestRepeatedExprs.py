"""Test evaluating expressions repeatedly comparing lldb against gdb."""

import os, sys
import unittest2
import lldb
import pexpect
from lldbbench import *

class RepeatedExprsCase(BenchBase):

    mydir = os.path.join("benchmarks", "example")

    def setUp(self):
        BenchBase.setUp(self)
        self.source = 'main.cpp'
        self.line_to_break = line_number(self.source, '// Set breakpoint here.')
        self.lldb_avg = None
        self.gdb_avg = None

    @benchmarks_test
    def test_compare_lldb_to_gdb(self):
        """Test repeated expressions with lldb vs. gdb."""
        self.buildDefault()
        self.exe_name = 'a.out'

        print
        self.run_lldb_repeated_exprs(self.exe_name, 100)
        print "lldb benchmark:", self.stopwatch
        self.run_gdb_repeated_exprs(self.exe_name, 100)
        print "gdb benchmark:", self.stopwatch
        print "lldb_avg/gdb_avg: %f" % (self.lldb_avg/self.gdb_avg)

    def run_lldb_repeated_exprs(self, exe_name, count):
        exe = os.path.join(os.getcwd(), exe_name)

        # Set self.child_prompt, which is "(lldb) ".
        self.child_prompt = '(lldb) '
        prompt = self.child_prompt

        # So that the child gets torn down after the test.
        self.child = pexpect.spawn('%s %s' % (self.lldbExec, exe))
        child = self.child

        # Turn on logging for what the child sends back.
        if self.TraceOn():
            child.logfile_read = sys.stdout

        child.expect_exact(prompt)
        child.sendline('breakpoint set -f %s -l %d' % (self.source, self.line_to_break))
        child.expect_exact(prompt)
        child.sendline('run')
        child.expect_exact(prompt)
        expr_cmd1 = 'expr ptr[j]->point.x'
        expr_cmd2 = 'expr ptr[j]->point.y'

        # Reset the stopwatch now.
        self.stopwatch.reset()
        for i in range(count):
            with self.stopwatch:
                child.sendline(expr_cmd1)
                child.expect_exact(prompt)
                child.sendline(expr_cmd2)
                child.expect_exact(prompt)
            child.sendline('process continue')
            child.expect_exact(prompt)        

        child.sendline('quit')
        try:
            self.child.expect(pexpect.EOF)
        except:
            pass

        self.lldb_avg = self.stopwatch.avg()
        if self.TraceOn():
            print "lldb expression benchmark:", str(self.stopwatch)
        self.child = None

    def run_gdb_repeated_exprs(self, exe_name, count):
        exe = os.path.join(os.getcwd(), exe_name)

        # Set self.child_prompt, which is "(gdb) ".
        self.child_prompt = '(gdb) '
        prompt = self.child_prompt

        # So that the child gets torn down after the test.
        self.child = pexpect.spawn('gdb %s' % exe)
        child = self.child

        # Turn on logging for what the child sends back.
        if self.TraceOn():
            child.logfile_read = sys.stdout

        child.expect_exact(prompt)
        child.sendline('break %s:%d' % (self.source, self.line_to_break))
        child.expect_exact(prompt)
        child.sendline('run')
        child.expect_exact(prompt)
        expr_cmd1 = 'print ptr[j]->point.x'
        expr_cmd2 = 'print ptr[j]->point.y'

        # Reset the stopwatch now.
        self.stopwatch.reset()
        for i in range(count):
            with self.stopwatch:
                child.sendline(expr_cmd1)
                child.expect_exact(prompt)
                child.sendline(expr_cmd2)
                child.expect_exact(prompt)
            child.sendline('continue')
            child.expect_exact(prompt)        

        child.sendline('quit')
        child.expect_exact('The program is running.  Exit anyway?')
        child.sendline('y')
        try:
            self.child.expect(pexpect.EOF)
        except:
            pass

        self.gdb_avg = self.stopwatch.avg()
        if self.TraceOn():
            print "gdb expression benchmark:", str(self.stopwatch)
        self.child = None


if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
