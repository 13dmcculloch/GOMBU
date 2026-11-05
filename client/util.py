#!/bin/python3

# douglas

import socket
import struct
import sys
import time

import enum

##########
# Network functions
##########

def recv_n(conn, n) -> bytes:
    # this must receive n bytes

    received_data = []
    n_bytes = 0
    while n_bytes < n:
        chunk = conn.recv(n)
        n_bytes += len(chunk)
        if n_bytes == 0:
            break

        received_data.append(chunk)
        #print("read", n_bytes, "bytes out of", n)

    # an interesting language
    result = [byte for chunk in received_data for byte in chunk]

    return bytes(result)


def recv_no(conn, n):

    return conn.recv(n)


def send_n(conn, data):
    # must send n bytes
    
    n_bytes = 0
    while n_bytes < len(data):
        n_bytes += conn.send(data)

    return n_bytes


def send_no(conn, data):

    n_bytes = conn.send(data)

    return n_bytes

#########
# Message functions
#########

class MessageType(enum.IntEnum):
    TEXT = 1
    DISCONNECT = 2
    VERIFY = 3
    USERNAME = 4
    REQUEST_LOGS = 5
    KA_ENQ = 6
    KA_ACK = 7


def serialise_message(data: bytes, message_type) -> bytes:
    # field     length      order
    # length    2           BE
    # type      1           
    # contents  < 65 K

    total_length = len(data) + 1
    if total_length > 65535:
        print("serialise_message: length", str(total_length),
            "exceeds maximum", str(65535))
        return b''

    length_b = struct.pack("!H", total_length)
    type_b = struct.pack("!B", message_type)

    message_bytes = type_b + data

    return (length_b, message_bytes)


def deserialise_message(data: bytes):
    # data does not contain length field
    type_b = data[0:1]  # what a spastic language
    message_type = struct.unpack("!B", type_b)[0]
    message_data = data[1:]

    message = Message(message_type, message_data)

    message.length = len(data)

    return message  # ready to be inspected, reserialised, and sent


class Message:
    def __init__(self, message_type, message_data, message_length_b = b''):
        self.length_b = message_length_b
        self.data = message_data
        self.type = message_type
        self.length = 0
        self.username = ''

        if message_length_b == b'':
            self.serialised = False


    def serialise(self):
        s = serialise_message(self.data, self.type)
        self.length_b = s[0]
        self.serialised = s[1]

        self.length = len(self.serialised)


    def log(self, path=""):
        if "linux" in sys.platform and path == "":
            path = "/tmp/vuh_chat_logs"
        elif path == "":
            return  # can't really do anything default here

        cur_time = time.strftime("%Y-%m-%d %H:%M:%S")
        f = open(path, "a")
        f.write(chr(31) + cur_time + ": " + self.username + ": " + self.data.decode('utf-8'))
        f.write('\n')
        f.close()


def send_message(conn, 
    data: bytes,
    message_type):

    message = Message(message_type, data)
    message.serialise()

    if send_n(conn, message.length_b) != 2:
        print("send_message: length bytes not sent properly")
    if send_n(conn, message.serialised) != message.length:
        print("send_message: message data not sent properly")


def receive_message(conn):
    # first get the message length
    length_b = recv_n(conn, 2)  # ushort
    if length_b == b'':
        return None

    length = struct.unpack("!H", length_b)[0]

    data = recv_n(conn, length)
    if data == b'':
        return None 

    message = deserialise_message(data)

    return message


##########
# User ID functions
##########

class User:  # sort of a struct for the connected_clients dict
    def __init__(self, addr):
        self.addr = addr
        self.username = "Anonymous"
