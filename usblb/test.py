#! /usr/bin/python

import serial
import unittest

class TestAcmLoopback(unittest.TestCase):

    def setUp(self):
        self.host = serial.Serial("/dev/ttyACM0")
        self.gadget = serial.Serial("/dev/ttyGS0")
        self.data = "".join(chr(c) for c in xrange(0x100))

    def readback(self, a, b, data):
        a.write(data)
        readback = b.read(len(data))
        self.assertEqual(data, readback)

    def test_h2g(self):
        self.readback(self.host, self.gadget, self.data)

    def test_g2h(self):
        self.readback(self.gadget, self.host, self.data)

if __name__ == '__main__':
    unittest.main()

if __name__ == "__main__":
	unittest.main()
