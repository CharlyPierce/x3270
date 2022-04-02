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
# b3270 smoke tests

import requests
from subprocess import Popen, PIPE, DEVNULL
import unittest
import Common.Test.cti as cti

class TestB3270Smoke(cti.cti):

    # b3270 NVT smoke test
    def test_b3270_nvt_smoke(self):

        # Start 'nc' to read b3270's output.
        nc = cti.copyserver()

        # Start b3270.
        b3270 = Popen(cti.vgwrap(['b3270']), stdin=PIPE, stdout=DEVNULL)
        self.children.append(b3270)

        # Feed b3270 some actions.
        b3270.stdin.write(b'<b3270-in>\n')
        b3270.stdin.write(f'<run actions="Open(a:c:t:127.0.0.1:{nc.port}) String(abc) Enter() Disconnect()"/>\n'.encode('utf8'))
        b3270.stdin.flush()

        # Make sure they are passed through.
        out = nc.data()
        self.assertEqual(b'abc\r\n', out)

        # Wait for the processes to exit.
        b3270.stdin.write(b'</b3270-in>\n')
        b3270.stdin.close()
        self.vgwait(b3270)

    # b3270 NVT smoke test
    @unittest.skipIf(True, 'Test used to exercise test infra')
    def test_b3270_crash(self):

        # Start b3270.
        hport, socket = cti.unused_port()
        b3270 = Popen(cti.vgwrap(['b3270', '-httpd', str(hport)]), stdin=PIPE, stdout=DEVNULL)
        self.children.append(b3270)
        self.check_listen(hport)

        # Feed b3270 some actions.
        r = requests.get(f'http://127.0.0.1:{hport}/3270/rest/json/Crash(null)')
        self.assertTrue(r.ok)

        # Wait for the process to exit.
        b3270.stdin.close()
        self.vgwait(b3270)

if __name__ == '__main__':
    unittest.main()
