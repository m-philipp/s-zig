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
		# start TCP_Server on Port 8000
		self.p.stdin.write(struct.pack(">BBH", 19,2,8000))
		self.assertTrue(struct.unpack(">BB", self.p.stdout.read(2)) == (19, 0))
		
		time.sleep(2)

		# Connect to Jennic on Jennic Port 8000
		# local PC-Addr: fe80::98ba:4fff:fe62:342a/64 (from ifconfig)
		print "Python: Connect to Jennic on Port 8000"
		addrinfo = getaddrinfo('fe80::206:98ff:fe00:232%tap0', 8000, AF_INET6, SOCK_STREAM)
		(family, socktype, proto, canonname, sockaddr) = addrinfo[0]
		s = socket(family, socktype, proto)
		s.settimeout(0.1)
		s.connect(sockaddr)
		
		# Send some bytes to Jennic
		print "Python: Send some bytes via Telnet"
		s.send("blaaaaa")
		time.sleep(2)

		# Read Callback from Jennic
		print "Python: Read Callback from Jennic"
		opcode,length,ip,port=struct.unpack(">BB16sH", self.p.stdout.read(20))
		print "Python: opcode: " , hex(opcode)
		print "Python: length: " , hex(length)
		# print ip addr
		print "Python: Callback IP:"
		print "".join([hex(ord(x))[2:] for x in ip])
		print "Python: Callback Port:", port	
		
		# Ask Jennic for TCP Data
		print "Python: Ask if the Jennic has some TCP Data received"
		self.p.stdin.write(struct.pack(">BB16sH", 16,18,ip,port))
		time.sleep(1)

		# Read the TCP_data the Jennic revieced
		print "Python: Read the TCP Data the Jennic received"
		opcode,length=struct.unpack(">BB", self.p.stdout.read(2))
		print "Python: opcode: " , hex(opcode)
		print "Python: length: " , hex(length)
		payload = struct.unpack(">7s", self.p.stdout.read(7))
		print payload
		#while True:
		#	time.sleep(0.01)

		self.assertTrue(True)
	
	def tearDown(self):
		self.p.kill()


if __name__ == '__main__':
	unittest.main()
