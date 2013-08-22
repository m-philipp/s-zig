import subprocess
import time
import struct
from socket import *



# while True:
#   print p.stdout.readline()

import unittest

class TestSuite(unittest.TestCase):

	def setUp(self):
		self.p=subprocess.Popen(['./plastic-sense.minimal-net'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
		while "*******Contiki 2.5 online*******" not in self.p.stdout.readline():
			pass


	def test_startServer(self):
		self.p.stdin.write(struct.pack(">BBH", 19,2,8000))
		self.assertTrue(struct.unpack(">BB", self.p.stdout.read(2)) == (19, 0))
		
		time.sleep(2)

		addrinfo = getaddrinfo('fe80::206:98ff:fe00:232%tap0', 8000, AF_INET6, SOCK_STREAM)
		(family, socktype, proto, canonname, sockaddr) = addrinfo[0]
		s = socket(family, socktype, proto)
		s.settimeout(0.1)
		s.connect(sockaddr)
		
		s.send("blaaaaa")
		# s.recv(5) # read 5 bytes
		time.sleep(2)
		opcode,length,ip,port=struct.unpack(">BB16sH", self.p.stdout.read(20))
		print "".join([hex(ord(x))[2:] for x in ip])
		#self.assertTrue(struct.unpack(">BBLLLLH", self.p.stdout.read(2)) == (22, 12,))

		self.assertTrue(True)
	
	def tearDown(self):
		self.p.kill()


if __name__ == '__main__':
	unittest.main()
