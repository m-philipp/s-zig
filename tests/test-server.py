import subprocess
import time
import struct
from socket import *
import netifaces as ni
from threading import Thread

import unittest

class TestSuite(unittest.TestCase):

	def getIp(self):
		localIpAddr = ni.ifaddresses('tap0')[10][0]['addr']
		localIpAddr = localIpAddr.split("%")
		localIpAddr = localIpAddr[0].split(":")
		for i in range(0, len(localIpAddr)):
			if localIpAddr[i] == '':
				localIpAddr[i] = '0000'
				while len(localIpAddr) < 8:
					localIpAddr.insert(i, '0000')
			if len(localIpAddr[i]) < 4:
				# TODO fix e.g. 003f
				localIpAddr[i] = '0' + localIpAddr[i]
		returnValue = []
		for i in range(0, len(localIpAddr)):
			returnValue.append(int(localIpAddr[i][0:2], 16))
			returnValue.append(int(localIpAddr[i][2:4], 16))
		return returnValue

	def setUp(self):
		self.p=subprocess.Popen(['./../plastic-sense.minimal-net'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
		while "*******Contiki 2.5 online*******" not in self.p.stdout.readline():
			pass

	# Test Case:
	# Start Server, Recieve some TCP Data, Send some TCP Data
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
                payload = ""
                for i in range(20):
                    payload += "0123456789"
		s.send(payload)
		time.sleep(0.05)

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
		self.p.stdin.write(struct.pack(">BB16sHB", 16,19,ip,port,127))
		time.sleep(0.1)

		# Read the TCP_data the Jennic revieced
		print "Python: Read the TCP Data the Jennic received (reaction on callback)"
		opcode,length=struct.unpack(">BB", self.p.stdout.read(2))
		print "Python: opcode: " , hex(opcode)
		print "Python: length: " , hex(length)
		payload = struct.unpack(">127s", self.p.stdout.read(127))
		print "Python: payload: " , payload
		
                # --------- read 2nd block

		# Read Callback from Jennic
		print "Python: Read Callback from Jennic"
		opcode,length,ip,port=struct.unpack(">BB16sH", self.p.stdout.read(20))
		print "Python: opcode: " , hex(opcode)
		print "Python: length: " , hex(length)
		# print ip addr
		print "Python: Callback IP:"
		print "".join([hex(ord(x))[2:] for x in ip])
		print "Python: Callback Port:", port	
		
                # Ask Jennic for 2 TCP Data Block
		print "Python: Ask if the Jennic has some TCP Data received 2.time"
		self.p.stdin.write(struct.pack(">BB16sHB", 16,19,ip,port,73))
		time.sleep(0.1)

		# Read the TCP_data the Jennic revieced
		print "Python: Read the TCP Data the Jennic received"
		opcode,length=struct.unpack(">BB", self.p.stdout.read(2))
		print "Python: opcode: " , hex(opcode)
		print "Python: length: " , hex(length)
		payload = struct.unpack(">73s", self.p.stdout.read(73))
		print "Python: payload: " , payload
		#while True:
		#	time.sleep(0.01)



                # SEND SERIAL ---> TELNET
                payload = "0123456"
		# let the Jennic send some bytes back via tcp
		self.p.stdin.write(struct.pack(">BB16sH7s", 14,25,ip,port,payload))
		print "Python: Send Opcode to Send Data"
		time.sleep(1)
		recievedPayload = s.recv(128)
		print "Python: socket recieved: " , recievedPayload
		self.assertTrue(True)
	
	def tearDown(self):
		self.p.kill()


if __name__ == '__main__':
	unittest.main()
