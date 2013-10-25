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
		self.p=subprocess.Popen(['./plastic-sense.minimal-net'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
		while "*******Contiki 2.5 online*******" not in self.p.stdout.readline():
			pass


	# Test Case:
	# connect to Server send and recieve some Data
	def dont_test_connectToServer(self):
		#create an INET, STREAMing socket
		serverSocket = socket(AF_INET6, SOCK_STREAM)
		serverSocket.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
    		#bind the socket to a public host,
    		# and a well-known port

		localIpAddr = ni.ifaddresses('tap0')[10][0]['addr']
		localIpAddr = localIpAddr.split("%")
		localIpAddr = localIpAddr[0].split(":")
		for i in range(0, len(localIpAddr)):
			if localIpAddr[i] == '':
				localIpAddr[i] = '0000'
				while len(localIpAddr) < 8:
					localIpAddr.insert(i, '0000')
			if len(localIpAddr[i]) < 4:
				localIpAddr[i] = '0' + localIpAddr[i]
		
		# print "".join([hex(ord(x))[2:] for x in ip])
		localIpAddr = "".join([x + ":" for x in localIpAddr])
		localIpAddr = localIpAddr[0:len(localIpAddr)-1]
		print "Python: Local IP Address: " + localIpAddr
		
		#TODO get Python listening on tap0
		# serversocket.bind((localIpAddr, 8000,0,0))
		serverSocket.bind(('', 8000))
		serverSocket.listen(5)
		
		clientSocket = None
		clientAddress = None
		ip = self.getIp()
		#time.sleep(0.1)
		def foo(clientSocket, clientAdress, ip, self):
			print "Python: establishing Connection."
			(clientSocket, clientAddress) = serverSocket.accept()
			print "Python: Connection established"
		
			time.sleep(3)
			
			# let the Jennic send some btes via tcp
			payload = "blaaa"
			print "Python: send Jennic some bytes to transmit."
			self.p.stdin.write(struct.pack(">BB16BH5s", 14,23,ip[0],ip[1],ip[2],ip[3],ip[4],ip[5],ip[6],ip[7],ip[8],ip[9],ip[10],ip[11],ip[12],ip[13],ip[14],ip[15],port,payload))
			time.sleep(1)
			opcode,length=struct.unpack(">BB", self.p.stdout.read(2))
			print "Python: opcode: ", opcode, " length: ", length
			time.sleep(1)
			recievedPayload = clientSocket.recv(128)
			print "Python: socket recieved: " , recievedPayload
			self.assertTrue(True)
		
		t = Thread(target=foo, args=(clientSocket, clientAddress,ip,self))
		t.start()
		print "Python: Setup Python Server on Port 8000"
		time.sleep(0.2)
		
		port = 8000
		self.p.stdin.write(struct.pack(">BB16BH", 13,18,ip[0],ip[1],ip[2],ip[3],ip[4],ip[5],ip[6],ip[7],ip[8],ip[9],ip[10],ip[11],ip[12],ip[13],ip[14],ip[15],port))
		print "Python: send Jennic opcode to connect to Server"

		# Join the listenin Thread back in the main thread
		# t.join()


		self.assertTrue(struct.unpack(">BBB", self.p.stdout.read(3)) == (13, 1, 1))
		print "Python: got connection established from Jennic"
		
		
		# time.sleep(6)
		while True:
			time.sleep(2)


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
		self.p.stdin.write(struct.pack(">BB16sHB", 16,19,ip,port,7))
		time.sleep(1)

		# Read the TCP_data the Jennic revieced
		print "Python: Read the TCP Data the Jennic received"
		opcode,length=struct.unpack(">BB", self.p.stdout.read(2))
		print "Python: opcode: " , hex(opcode)
		print "Python: length: " , hex(length)
		payload = struct.unpack(">7s", self.p.stdout.read(7))
		print "Python: payload: " , payload
		#while True:
		#	time.sleep(0.01)

		# let the Jennic send some bytes back via tcp
		self.p.stdin.write(struct.pack(">BB16sH7s", 14,25,ip,port,payload[0]))
		print "Python: Send Opcode to Send Data"
		time.sleep(1)
		recievedPayload = s.recv(128)
		print "Python: socket recieved: " , recievedPayload
		self.assertTrue(True)
	
	def tearDown(self):
		self.p.kill()


if __name__ == '__main__':
	unittest.main()
