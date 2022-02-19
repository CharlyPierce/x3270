#!/usr/bin/env python3
#
# Copyright (c) 2021-2022 Paul Mattes.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the names of Paul Mattes nor the names of his contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# c3270 smoke tests

import unittest
from subprocess import Popen, PIPE, DEVNULL
import requests
import sys
if not sys.platform.startswith('win'):
    import pty
import os
import time
import re
import os.path
import Common.Test.ct as ct

@unittest.skipIf(sys.platform == "darwin", "Not ready for c3270 graphic tests")
@unittest.skipIf(sys.platform.startswith('win'), "Windows uses different c3270 graphic tests")
class TestC3270Smoke(unittest.TestCase):

    # Set up procedure.
    def setUp(self):
        self.children = []

    # Tear-down procedure.
    def tearDown(self):
        # Tidy up the children.
        for child in self.children:
            child.kill()
            child.wait()

    # c3270 3270 smoke test
    def test_c3270_3270_smoke(self):

        # Start 'playback' to read s3270's output.
        playback_port, ts = ct.unused_port()
        playback = Popen(["playback", "-w", "-p", str(playback_port),
            "c3270/Test/ibmlink2.trc"], stdin=PIPE, stdout=DEVNULL)
        self.children.append(playback)
        ct.check_listen(playback_port)
        ts.close()

        # Fork a child process with a PTY between this process and it.
        c3270_port, ts = ct.unused_port()
        os.environ['TERM'] = 'xterm-256color'
        (pid, fd) = pty.fork()
        if pid == 0:
            # Child process
            ts.close()
            os.execvp(ct.vgwrap_ecmd('c3270'),
                ct.vgwrap_eargs(["c3270", "-model", "2", "-utf8",
                    "-httpd", f"127.0.0.1:{c3270_port}",
                    f"127.0.0.1:{playback_port}"]))

        # Parent process.
        
        # Make sure c3270 started.
        ct.check_listen(c3270_port)
        ts.close()

        # Write the stream to c3270.
        playback.stdin.write(b'r\nr\nr\nr\nr\n')
        playback.stdin.flush()
        ct.check_push(playback, c3270_port, 1)
        playback.stdin.write(b'e\n')
        playback.stdin.flush()

        # Collect the output.
        result = ''
        while True:
            try:
                rbuf = os.read(fd, 1024)
            except OSError:
                break
            result += rbuf.decode('utf8')
        
        # Make the output a bit more readable and split it into lines.
        # Then replace the text that varies with the build with tokens.
        result = result.replace('\x1b', '<ESC>').split('\n')
        result[0] = re.sub(' v.*\r', '<version>\r', result[0], count=1)
        result[1] = re.sub(' 1989-.* by', ' <years> by', result[1], count=1)
        for i in range(len(result)):
            result[i] = re.sub(' port [0-9]*\.\.\.', ' <port>...', result[i], count=1)
        rtext = '\n'.join(result)
        if 'GENERATE' in os.environ:
            # Use this to regenerate the template file.
            file = open(os.environ['GENERATE'], "w")
            file.write(rtext)
            file.close()
        else:
            # Compare what we just got to the reference file.
            localtext = f'c3270/Test/smoke_{sys.platform}.txt'
            if os.path.exists(localtext):
                text = localtext
            else:
                text = 'c3270/Test/smoke.txt'
            file = open(text, "r", newline='')
            ctext = file.read()
            file.close()
            self.assertEqual(rtext, ctext)

        playback.stdin.close()
        playback.kill()
        playback.wait(timeout=2)
        ct.vgwait_pid(pid)

if __name__ == '__main__':
    unittest.main()
