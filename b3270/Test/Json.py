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
# b3270 JSON tests

import unittest
from subprocess import Popen, PIPE, DEVNULL
import json
import TestCommon

class TestB3270Json(unittest.TestCase):

    # b3270 NVT JSON smoke test
    def test_b3270_nvt_json_smoke(self):

        # Start 'nc' to read b3270's output.
        nc = Popen(["python3", "Common/Test/nc1.py", "127.0.0.1", "9991"],
                stdout=PIPE)
        TestCommon.check_listen(9991)

        # Start b3270.
        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=DEVNULL)

        # Feed b3270 some actions.
        j = { "run": { "actions": [ { "action": "Open", "args": ["a:c:t:127.0.0.1:9991"]}, { "action": "String", "args": ["abc"] }, { "action": "Enter" }, { "action": "Disconnect" } ] } }
        b3270.stdin.write(json.dumps(j).encode('utf8') + b'\n')
        b3270.stdin.flush()

        # Make sure they are passed through.
        out = nc.stdout.read()
        self.assertEqual(b"abc\r\n", out)

        # Wait for the processes to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)
        nc.stdout.close()
        nc.wait(timeout=2)

    # b3270 JSON single test
    def test_b3270_json_single(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)

        # Feed b3270 an action.
        b3270.stdin.write(b'"set startTls"\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('run-result' in out)
        result = out['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('true', result['text'][0])

    # b3270 JSON multiple test
    def test_b3270_json_multiple(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)

        # Feed b3270 two actions, which it will run concurrently and complete
        # in reverse order.
        b3270.stdin.write(b'"Wait(0.1,seconds) Set(startTls) Quit()" "Set(insertMode)"\n')
        b3270.stdin.flush()
        b3270.wait(timeout=2)

        # Get the result.
        out = b3270.communicate(timeout=2)[0].decode('utf8').split('\n')
        insert_mode = json.loads(out[-3]) # false
        start_tls = json.loads(out[-2])   # true

        # Check.
        self.assertTrue('run-result' in insert_mode)
        result = insert_mode['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('false', result['text'][0])

        self.assertTrue('run-result' in start_tls)
        result = start_tls['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('true', result['text'][0])

    # b3270 JSON split-line test
    def test_b3270_json_split(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)

        # Feed b3270 an action.
        b3270.stdin.write(b'{\n"run"\n:{"actions"\n:{"action":"set"\n,"args":["startTls"]\n}}}\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('run-result' in out)
        result = out['run-result']
        self.assertTrue('success' in result)
        self.assertTrue(result['success'])
        self.assertTrue('text' in result)
        self.assertEqual('true', result['text'][0])

    # b3270 JSON semantic error test
    def test_b3270_json_semantic_error(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE)

        # Feed b3270 an action.
        b3270.stdin.write(b'{\n"run"\n:{"actiobs"\n:{"action":"set"\n,"args":["startTls"]\n}}}\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('ui-error' in out)
        ui_error = out['ui-error']
        self.assertTrue('fatal' in ui_error)
        self.assertFalse(ui_error['fatal'])

    # b3270 JSON syntax error test
    def test_b3270_json_syntax_error(self):

        b3270 = Popen(['b3270', '-json'], stdin=PIPE, stdout=PIPE,
                stderr=DEVNULL)

        # Feed b3270 an action.
        b3270.stdin.write(b'{\n"run"\n:{"actiobs"\n:{"action":"set"\n,"args":["startTls"]\n}}?\n')

        # Get the result.
        out = json.loads(b3270.communicate(timeout=2)[0].split(b'\n')[-2].decode('utf8'))

        # Wait for the process to exit.
        b3270.stdin.close()
        b3270.wait(timeout=2)

        # Check.
        self.assertTrue('ui-error' in out)
        ui_error = out['ui-error']
        self.assertTrue('fatal' in ui_error)
        self.assertTrue(ui_error['fatal'])

if __name__ == '__main__':
    unittest.main()