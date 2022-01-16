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
# s3270 auto-JSON tests

import unittest
from subprocess import Popen, PIPE, DEVNULL
import json
import socket
import TestCommon

class TestS3270Json(unittest.TestCase):

    # Check a JSON-formatted result.
    def check_result_json(self, out):
        j = json.loads(out)
        self.assertEqual('true', j['result'][0])
        self.assertTrue(j['success'])
        self.assertTrue(j['status'].startswith('L U U N N 4 24 80 0 0 0x0 '))

    # Check an s3270-formatted result.
    def check_result_s3270(self, out):
        self.assertEqual('data: true', out[0])
        self.assertTrue(out[1].startswith('L U U N N 4 24 80 0 0 0x0 '))
        self.assertEqual('ok', out[2])

    # Read from a socket until EOF.
    def recv_to_eof(self, s):
        result = b''
        while True:
            r = s.recv(1024)
            if len(r) == 0:
                break
            result += r
        return result.decode('utf8')

    # s3270 basic stdin JSON test
    def test_s3270_stdin_json(self):

        # Start s3270.
        s3270 = Popen(["s3270"], stdin=PIPE, stdout=PIPE)

        # Push a JSON-formatted command at it.
        command = json.dumps({'action':'Set','args':['startTls']}).encode('utf8') + b'\n'
        s3270.stdin.write(command)

        # Decode the result.
        stdout = s3270.communicate()[0].decode('utf8')

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        self.check_result_json(stdout)

    # s3270 basic socket JSON test
    def test_s3270_socket_json(self):

        # Start s3270.
        s3270 = Popen(["s3270", '-scriptport', '9981', '-scriptportonce'])
        TestCommon.check_listen(9981)

        # Push a JSON-formatted command at it.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', 9981))
        command = json.dumps({'action':'Set','args':['startTls']}).encode('utf8') + b'\n'
        s.sendall(command)
        s.shutdown(socket.SHUT_WR)

        # Decode the result.
        result = self.recv_to_eof(s)
        s.close()

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        self.check_result_json(result)

    # s3270 multi-line stdin JSON test
    def test_s3270_stdin_multiline_json(self):

        # Start s3270.
        s3270 = Popen(["s3270"], stdin=PIPE, stdout=PIPE)

        # Push a JSON-formatted command at it.
        command = json.dumps({'action':'Set','args':['startTls']}).replace(' ', '\n').encode('utf8') + b'\n'
        s3270.stdin.write(command)

        # Decode the result.
        stdout = s3270.communicate()[0].decode('utf8')

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        self.check_result_json(stdout)

    # s3270 basic stdin JSON string test
    def test_s3270_stdin_json_string(self):

        # Start s3270.
        s3270 = Popen(["s3270"], stdin=PIPE, stdout=PIPE)

        # Push a JSON-formatted command at it.
        command = json.dumps('Set(startTls)').encode('utf8') + b'\n'
        s3270.stdin.write(command)

        # Decode the result.
        stdout = s3270.communicate()[0].decode('utf8')

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        self.check_result_json(stdout)

    # s3270 socket JSON string test
    def test_s3270_socket_json_string(self):

        # Start s3270.
        s3270 = Popen(["s3270", '-scriptport', '9981', '-scriptportonce'])
        TestCommon.check_listen(9981)

        # Push a JSON-formatted command at it.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', 9981))
        command = json.dumps('Set(startTls)').encode('utf8') + b'\n'
        s.sendall(command)
        s.shutdown(socket.SHUT_WR)

        # Decode the result.
        result = self.recv_to_eof(s)
        s.close()

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        self.check_result_json(result)

    # s3270 JSON semantic error test
    def test_s3270_stdin_json_bad_semantics(self):

        # Start s3270.
        s3270 = Popen(["s3270"], stdin=PIPE, stdout=PIPE)

        # Push a bad JSON-formatted command at it.
        command = json.dumps({'foo':'bar'}).encode('utf8') + b'\n'
        s3270.stdin.write(command)

        # Decode the result.
        stdout = s3270.communicate()[0].decode('utf8')

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output, which is in JSON format.
        j = json.loads(stdout)
        self.assertEqual("Missing struct element 'action'", j['result'][0])
        self.assertFalse(j['success'])
        self.assertTrue(j['status'].startswith('L U U N N 4 24 80 0 0 0x0 '))

    # s3270 socket JSON semantic error test
    def test_s3270_socket_json_semantic_error(self):

        # Start s3270.
        s3270 = Popen(["s3270", '-scriptport', '9981', '-scriptportonce'])
        TestCommon.check_listen(9981)

        # Push a JSON-formatted command at it.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', 9981))
        command = json.dumps({'foo':'bar'}).encode('utf8') + b'\n'
        s.sendall(command)
        s.shutdown(socket.SHUT_WR)

        # Decode the result.
        result = self.recv_to_eof(s)
        s.close()

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        j = json.loads(result)
        self.assertEqual("Missing struct element 'action'", j['result'][0])
        self.assertFalse(j['success'])
        self.assertTrue(j['status'].startswith('L U U N N 4 24 80 0 0 0x0 '))

    # s3270 JSON syntax error test
    def test_s3270_stdin_json_syntax(self):

        # Start s3270.
        s3270 = Popen(["s3270"], stdin=PIPE, stdout=PIPE)

        # Push a bad syntax JSON-formatted command at it.
        s3270.stdin.write(b'{"foo"}\n')

        # Decode the result.
        stdout = s3270.communicate()[0].decode('utf8').split('\n')

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output, which is in s3270 format.
        # This is because JSON parsing is inferred from the first
        # non-whitespace character on the line, and that guess might be wrong.
        self.assertEqual(4, len(stdout))
        self.assertTrue(stdout[0].startswith('data: JSON parse error'))
        self.assertTrue(stdout[1].startswith('L U U N N 4 24 80 0 0 0x0 '))
        self.assertEqual('error', stdout[2])
        self.assertEqual('', stdout[3])

    # s3270 socket JSON syntax error test
    def test_s3270_socket_json_syntax_error(self):

        # Start s3270.
        s3270 = Popen(["s3270", '-scriptport', '9981', '-scriptportonce'])
        TestCommon.check_listen(9981)

        # Push a JSON-formatted command at it.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', 9981))
        s.sendall(b'{"foo"}\n')
        s.shutdown(socket.SHUT_WR)

        # Decode the result.
        result = self.recv_to_eof(s).split('\n')
        s.close()

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output, which is in s3270 format.
        # This is because JSON parsing is inferred from the first
        # non-whitespace character on the line, and that guess might be wrong.
        self.assertEqual(4, len(result))
        self.assertTrue(result[0].startswith('data: JSON parse error'))
        self.assertTrue(result[1].startswith('L U U N N 4 24 80 0 0 0x0 '))
        self.assertEqual('error', result[2])
        self.assertEqual('', result[3])

    # s3270 JSON/s3270 mode-switching test.
    def test_s3270_stdin_mode_switch(self):

        # Start s3270.
        s3270 = Popen(["s3270"], stdin=PIPE, stdout=PIPE)

        # Push s3270, then JSON, then s3270, then JSON at it.
        s3270.stdin.write(b'Set(startTls)\n')
        command = json.dumps({'action':'Set','args':['startTls']}).replace(' ', '\n').encode('utf8') + b'\n'
        s3270.stdin.write(command)
        s3270.stdin.write(b'Set(startTls)\n')
        s3270.stdin.write(command)

        # Decode the result.
        stdout = s3270.communicate()[0].decode('utf8').split('\n')

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        # First three lines are s3270, next line is JSON, next three are s3270,
        # last is JSON.
        self.check_result_s3270(stdout[0:3])
        self.check_result_json(stdout[3])
        self.check_result_s3270(stdout[4:7])
        self.check_result_json(stdout[7])

    # s3270 socket JSON mode-switch test
    def test_s3270_socket_json_mode_switch(self):

        # Start s3270.
        s3270 = Popen(["s3270", '-scriptport', '9981', '-scriptportonce'])
        TestCommon.check_listen(9981)

        # Push s3270, then JSON, then s3270, then JSON at it.
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', 9981))
        s.sendall(b'Set(startTls)\n')
        command = json.dumps({'action':'Set','args':['startTls']}).encode('utf8') + b'\n'
        s.sendall(command)
        s.sendall(b'Set(startTls)\n')
        s.sendall(command)
        s.shutdown(socket.SHUT_WR)

        # Decode the result.
        result = self.recv_to_eof(s).split('\n')
        s.close()

        # Wait for the process to exit successfully.
        rc = s3270.wait()
        self.assertEqual(0, rc)

        # Test the output.
        # First three lines are s3270, next line is JSON, next three are s3270,
        # last is JSON.
        self.check_result_s3270(result[0:3])
        self.check_result_json(result[3])
        self.check_result_s3270(result[4:7])
        self.check_result_json(result[7])

if __name__ == '__main__':
    unittest.main()