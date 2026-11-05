#!/bin/python3

# adapted from echo example
# https://docs.python.org/3/library/selectors.html
# douglas 2025

import socket
import selectors
import sys
import signal
import struct
import threading
import math
import time
import os

import util

##########
# Globals
##########

KA_DELAY = 1

sel = selectors.DefaultSelector()
stats_lock = threading.Lock()
log_lock = threading.Lock()
clients_lock = threading.Lock()
f_shutdown = False

g_chatlog_filename = "chatlog_" + time.strftime('%Y-%m-%d')

##########
# Server thread targets
##########

def keepalive(connected_clients):
    while not f_shutdown:
        dead_clients = []

        clients_lock.acquire()
        for conn in connected_clients.keys():
            util.send_message(conn, chr(5).encode("utf-8"), 
                util.MessageType.KA_ENQ)
        clients_lock.release()

        time.sleep(KA_DELAY)


##########
# Server functions
##########

def remove_client(conn, connected_clients):
    sel.unregister(conn)
    connected_clients.pop(conn)
    conn.close()


def broadcast(data, connected_clients, post_events=''):
    n_event = 0
    n_bytes = 0
    clients_lock.acquire()
    for conn in connected_clients.keys():
        util.send_message(conn, data, util.MessageType.TEXT)
        n_event += 1
        n_bytes += len(data)
    clients_lock.release()

    if post_events == '':
        return

    n_clients = len(connected_clients.keys())
    stats_lock.acquire()
    post_events["mu_1"] += 1.0 * n_event
    post_events["mu_5"] += 0.449329 * n_event
    post_events["bytes_15"] += 1.0 * n_bytes
    post_events["bytes_60"] += 0.472367 * n_bytes
    stats_lock.release()
    

def shutdown(connected_clients, accept_sock, stats_t):
    print("Shutting down.")
    message = socket.gethostname() + ": Server is shutting down!"
    broadcast(bytes(message, encoding='utf-8'),
        connected_clients)

    global f_shutdown
    f_shutdown = True

    clients_lock.acquire()
    for conn in connected_clients.keys():
        sel.unregister(conn)
        conn.close()
    connected_clients.clear()
    clients_lock.release()

    sel.unregister(accept_sock)
    accept_sock.close()
    
    print("Closed all sockets.")

    #stats_t.join()

    sys.exit(0)
    

##########
# Server poll event routines
##########

def accept(sock, connected_clients, post_events, mask):
    conn, addr = sock.accept()  # Should be ready
    sel.register(conn, selectors.EVENT_READ, 
        (read, connected_clients, post_events))
    # verify

    # set username
    user = util.User(addr)
    clients_lock.acquire()
    connected_clients.update({conn: user})
    clients_lock.release()


def read(conn, connected_clients, post_events, mask):
    message = util.receive_message(conn)
    username = connected_clients[conn].username

    if message is None:
        clients_lock.acquire()
        remove_client(conn, connected_clients) 
        clients_lock.release()
        return

    message.username = username

    # deal with the message type
    if message.type == util.MessageType.TEXT:
        message.log(g_chatlog_filename)
        username += ": "
        payload = bytes(username, encoding='utf-8') + message.data
        broadcast(payload, connected_clients, post_events)

    elif message.type == util.MessageType.DISCONNECT:
        # the user intends to disconnect
        message.log(g_chatlog_filename)
        clients_lock.acquire()
        remove_client(conn, connected_clients) 
        clients_lock.release()

    elif message.type == util.MessageType.USERNAME:
        # set the username
        message.log(g_chatlog_filename)
        clients_lock.acquire()
        connected_clients[conn].username = str(message.data, 
            encoding='utf-8')
        clients_lock.release()

    elif message.type == util.MessageType.REQUEST_LOGS:
        # send the logs for this session to the client
        message.log(g_chatlog_filename)
        fp = open(g_chatlog_filename, 'r')
        n_lines = 0
        n_bytes = 0
        for line in fp:
            util.send_message(conn, bytes(line[:len(line)-1], 
                encoding='utf-8'),
                util.MessageType.TEXT)
            n_lines += 1
            n_bytes += len(line) - 1

        stats_lock.acquire()
        post_events["mu_1"] += 1.0 * n_lines
        post_events["mu_5"] += 0.449329 * n_lines
        post_events["bytes_15"] += 1.0 * n_bytes
        post_events["bytes_60"] += 0.472367 * n_bytes 
        stats_lock.release()

        fp.close()

    elif message.type == util.MessageType.VERIFY:
        message.log(g_chatlog_filename)
        if message.data != "chat server client":
            # failed validation, so disconnect (can't be bothered to
            # implement)
            util.send_message(conn, 
                bytes("Failed validation", encoding='utf-8'), 
                util.MessageType.TEXT)
            conn.close()

    #elif message.type == util.MessageType.KA_ACK:  # it exists

    # update post events
    stats_lock.acquire()
    post_events["mu_1"] += 1.0
    post_events["mu_5"] += 0.449329 
    post_events["bytes_15"] += 1.0 * message.length
    post_events["bytes_60"] += 0.472367 * message.length
    stats_lock.release()


##########
# Server statistics page
##########
def update_post_events(post_events, time_delta):
    # set up multiplier for post event
    mu_1 = post_events.get("mu_1")
    mu_5 = post_events.get("mu_5")
    bytes_15 = post_events.get("bytes_15")
    bytes_60 = post_events.get("bytes_60")

    mu_1 = mu_1 * math.exp(-1 * time_delta / 60)
    mu_5 = mu_5 * math.exp(-1 * time_delta / (60*5))
    bytes_15 = bytes_15 * math.exp(-1 * time_delta / (15))
    bytes_60 = bytes_60 * math.exp(-1 * time_delta / (15*4))

    # update post events
    stats_lock.acquire()
    post_events["mu_1"] = mu_1
    post_events["mu_5"] = mu_5
    post_events["bytes_15"] = bytes_15
    post_events["bytes_60"] = bytes_60
    stats_lock.release()


def print_statistics(connected_clients, post_events):
    print("\033[2J\r")

    buf = ""
    # title and context
    title = "\t\t\tChat Service v0.3\t\t\t\n"
    title += "\t\t\t-----------------\t\t\t"
    print(title)
    print(time.strftime("%Y-%m-%d %H:%M:%S"))

    buf += "Host:\t " + socket.gethostname() + " on TCP port " + str(post_events["port"]) + "\n"
    buf += "User:\t " + os.getlogin() + " (" + str(os.getuid()) + ")"
    print(buf)

    # post events
    mu_1 = round(post_events.get("mu_1"), 2)
    mu_5 = round(post_events.get("mu_5"), 2)
    bytes_15 = round(post_events.get("bytes_15"), 2)
    bytes_60 = round(post_events.get("bytes_60"), 2)

    print("----\nLoad averages")
    buf = "1 minute:\t " + str(mu_1) + " msg\n"
    buf += "5 minute:\t " + str(mu_5) + " msg\n"
    buf += "15 second:\t " + str(bytes_15) + " B\n"
    buf += "60 second:\t " + str(bytes_60) + " B\n"
    print(buf, end='')

    if "linux" in sys.platform:
        print("----\nSystem")
        fp = open("/proc/loadavg", "r")
        loadavg = fp.readline()
        fp.close()

        fp = open("/proc/meminfo", "r")
        status = fp.readlines()
        fp.close()
        for line in status:
            if "MemTotal" in line:
                memtotal = line
            elif "MemAvailable" in line:
                memavailable = line

        memtotal_i = int(memtotal[memtotal.rfind(" ", 0, memavailable.rfind(" ") - 1) + 1:memtotal.rfind(" ")])
        memavailable_i = int(memavailable[memavailable.rfind(" ", 0, memavailable.rfind(" ") - 1) + 1:memavailable.rfind(" ")])


        buf = "Load average:\t "
        buf += loadavg[:loadavg.rfind(" ")] + "\n"
        buf += memtotal + memavailable
        buf += "Memory usage:\t " + str(round(100 - 100 * memavailable_i / memtotal_i, 2)) + " %"
        print(buf)

        print("----\nProcess")
        fp = open("/proc/self/status", "r")
        status = fp.readlines()
        fp.close()
        for line in status:
            if "VmRSS" in line:
                VmRSS = line
            elif "VmHWM" in line:
                VmHWM = line
        VmRSS = VmRSS[VmRSS.rfind("\t") + 1:]
        VmHWM = VmHWM[VmHWM.rfind("\t") + 1:]

        buf = "Memory high water mark:\t\t " + VmHWM
        buf += "Current resident memory:\t " + VmRSS
        print(buf, end='')

    # connected clients
    buf = "\n\t\t\tConnected Clients\t\t\t\n"
    buf += "\t\t\t-----------------\t\t\t\n"
    clients_lock.acquire()  # make sure the dictionary does not change size
    # also change this to a connected_clients lock as stats has its own
    for conn, user in connected_clients.items():
        buf += user.username + " on " + str(user.addr) + "\n"
    clients_lock.release()

    print(buf, end="")


def statistics(connected_clients, post_events):
    # update post events stats and print the statistics page
    while True:
        if f_shutdown == True:
            break

        print_statistics(connected_clients, post_events)

        cur_time_s = time.time()
        time.sleep(2)
        time_delta = time.time() - cur_time_s
        update_post_events(post_events, time_delta)

    sys.exit(0)



##########
# Server main
##########

def server(port):
    sock = socket.socket()
    sock.bind(('', port))
    sock.listen(100)

    connected_clients = {}  # conn:  username
    post_events = {
        # port (bad programming)
        "port": port,

        # post averages
        "mu_1": 0.0,
        "mu_5": 0.0,

        # bytes averages
        "bytes_15": 0.0,
        "bytes_60": 0.0
    }

    stats_t = threading.Thread(target=statistics,
        args=(connected_clients, post_events))
    
    ka_t = threading.Thread(target=keepalive,  # problematic variable name
        args=(connected_clients,))

    #stats_t.start()
    ka_t.start()  # LOL

    sel.register(sock, selectors.EVENT_READ, 
        (accept, connected_clients, post_events))

    while True:
        try:
            events = sel.select()
        except KeyboardInterrupt:  # SIGINT throws this by default
            shutdown(connected_clients, sock, stats_t)

        for key, mask in events:
            func_name = key.data[0]

            if func_name == read or func_name == accept:
                func_name(key.fileobj, key.data[1], key.data[2],  mask)

def print_help():
    print("./server [listen port]")

    sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print_help()

    try:
        port = int(sys.argv[1])
    except:
        print_help()

    with open(g_chatlog_filename, 'w') as fp:
        pass

    server(int(sys.argv[1]))

    print("Exited.")
