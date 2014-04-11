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
                print "Python: setup minimal-net"
		self.p=subprocess.Popen(['./plastic-sense.minimal-net'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
		# while "*******Contiki 2.5 online*******" not in self.p.stdout.readline():
		while "*******" not in self.p.stdout.readline():
			pass


	# Test Case:
	# connect to Server send and recieve some Data
	def test_connectToServer(self):
                print "Python: starting testCase"
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
		        

			time.sleep(20) # sleep 20 Sec
		


		t = Thread(target=foo, args=(clientSocket, clientAddress,ip,self))
		t.start()
		print "Python: Setup Python Server on Port 8000"
		time.sleep(0.2)
		
		port = 8000
		self.p.stdin.write(struct.pack(">BB16BH", 13,18,ip[0],ip[1],ip[2],ip[3],ip[4],ip[5],ip[6],ip[7],ip[8],ip[9],ip[10],ip[11],ip[12],ip[13],ip[14],ip[15],port))
		print "Python: send Jennic opcode to connect to Server"

		# Join the listening Thread back in the main thread
		# t.join()

                
	        returnValue = struct.unpack(">BBB", self.p.stdout.read(3))
                print "Python: Jennic returned: " + str(returnValue);
		self.assertTrue(returnValue == (13, 1, 1))
		print "Python: got connection established from Jennic"
		
                print "Python: ask how much data is available (should be zero)"
		self.p.stdin.write(struct.pack(">BB16BH", 15,18,ip[0],ip[1],ip[2],ip[3],ip[4],ip[5],ip[6],ip[7],ip[8],ip[9],ip[10],ip[11],ip[12],ip[13],ip[14],ip[15],port))
		
	        returnValue = struct.unpack(">BBB", self.p.stdout.read(3))
                print "Python: Jennic returned: " + str(returnValue);
		self.assertTrue(returnValue == (15, 1, 0))

		# time.sleep(6)
		time.sleep(2)

	
	def tearDown(self):
		self.p.kill()


if __name__ == '__main__':
	unittest.main()
