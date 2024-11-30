#!/usr/bin/env python3
#
# Copyright (c) 2021-2024 Paul Mattes.
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
# s3270 extendedDataStream tests

import unittest
from subprocess import Popen, PIPE, DEVNULL
import requests
import os
import Common.Test.cti as cti

class TestS3270ExtendedDataStream(cti.cti):

    # s3270 extended data stream test
    def test_s3270_extended_data_stream(self):

        # Start s3270.
        port, ts = cti.unused_port()
        s3270 = Popen(cti.vgwrap(["s3270", "-httpd", f'{port}']))
        self.children.append(s3270)
        self.check_listen(port)
        ts.close()

        # Set the model with just a digit.
        r = requests.get(f'http://127.0.0.1:{port}/3270/rest/json/Set(model,3) Set(model) Show(TerminalName)')
        s = r.json()
        self.assertTrue(r.ok)
        result = r.json()['result']
        self.assertEqual(2, len(result))
        self.assertEqual('3279-3-E', result[0])
        self.assertEqual('IBM-3279-3-E', result[1])

        # Change extendedDataStream and make sure the model and terminal name change.
        r = requests.get(f'http://127.0.0.1:{port}/3270/rest/json/Set(extendedDataStream,false) Set(model) Show(TerminalName)')
        s = r.json()
        self.assertTrue(r.ok)
        result = r.json()['result']
        self.assertEqual(2, len(result))
        self.assertEqual('3279-3', result[0])
        self.assertEqual('IBM-3279-3', result[1])

        # Change extendedDataStream back and make sure the model and terminal name change back.
        r = requests.get(f'http://127.0.0.1:{port}/3270/rest/json/Set(extendedDataStream,true) Set(model) Show(TerminalName)')
        s = r.json()
        self.assertTrue(r.ok)
        result = r.json()['result']
        self.assertEqual(2, len(result))
        self.assertEqual('3279-3-E', result[0])
        self.assertEqual('IBM-3279-3-E', result[1])

        # Clear extendedDataStream, then set the model explicitly with -E, and make sure it disappears.
        r = requests.get(f'http://127.0.0.1:{port}/3270/rest/json/Set(extendedDataStream,false,model,3279-3-E) Set(model) Show(TerminalName)')
        s = r.json()
        self.assertTrue(r.ok)
        result = r.json()['result']
        self.assertEqual(2, len(result))
        self.assertEqual('3279-3', result[0])
        self.assertEqual('IBM-3279-3', result[1])

        # Wait for the process to exit.
        requests.get(f'http://127.0.0.1:{port}/3270/rest/json/Quit()')
        self.vgwait(s3270)

if __name__ == '__main__':
    unittest.main()
