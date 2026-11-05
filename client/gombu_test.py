#!/bin/python3

import client
import util

import sys
import socket

addr = sys.argv[1]
port = sys.argv[2]
sock = socket.create_connection((addr, port)) 
sock.setblocking(True)

util.send_message(sock, bytes("Testing.",encoding='utf-8'),
        util.MessageType.TEXT)
message = util.receive_message(sock)

print(str(message.data,encoding='utf-8'))
print(list(message.data))

util.send_message(sock, bytes("exit",encoding='utf-8'),
        util.MessageType.DISCONNECT)
