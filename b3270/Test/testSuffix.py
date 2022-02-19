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
# b3270 session file suffix tests

import unittest
import sys
import Common.Test.ct as ct
import Common.Test.suffix as suffix

class TestB3270Suffix(unittest.TestCase):

    # Set up procedure.
    def setUp(self):
        self.children = []

    # Tear-down procedure.
    def tearDown(self):
        # Tidy up the children.
        for child in self.children:
            child.kill()
            child.wait()

    def test_b3270_b3270(self):
        suffix.suffix_test('b3270', '.b3270', self.children)

    @unittest.skipUnless(sys.platform.startswith('win'), 'Windows-specific test')
    def test_b3270_wb3270(self):
        suffix.suffix_test('b3270', '.wb3270', self.children)

    @unittest.skipUnless(sys.platform.startswith('win'), 'Windows-specific test')
    def test_b3270_wb3(self):
        suffix.suffix_test('b3270', '.wb3', self.children)

if __name__ == '__main__':
    unittest.main()
