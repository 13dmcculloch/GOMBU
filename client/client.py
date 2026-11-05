#!/bin/python3

# douglas 2025

# serialise vuh chat logs and ensure that every user has the same chat log
# file over the network and at start-up by sending them all messages stored
# on server for history
# store and read from vuh chat log file every time message is received
# to update widget

# basic imports
import socket
import sys
import threading
import struct
import tempfile
import time

# gore imports
import tkinter as tk

# file imports
import util

log_lock = threading.Lock()
f_connected = False
f_exited = False

def verify(sock):
    # the client sends some sort of code to the server to allow it to
    # continue connection with the server
    # in theory this prevents other applications from forging message
    # packets and causing issues, such as incorrect message length
    # fields, which would cause errors if over max length, immediate 
    # disconnect if longer than message contents, and incomplete messages
    # and subsequent cases of the above

    # the code is a magic number of length known to both the client
    # and server. its inequality will result in disconnection
    pass


def set_username(sock, username):
    if username == "" or username == "\n":  # we can skip this
        return
    username = bytes(username, encoding='utf-8')

    util.send_message(sock, username, util.MessageType.USERNAME)


def disconnect(sock):
    util.send_message(sock, 
        bytes("exit", encoding='utf-8'), 
        util.MessageType.DISCONNECT)

    f_connected = False

    sock.close()


def send_message(sock, user_input):
    if user_input[0] == "/":
        if user_input[1:user_input.find(" ")] == "username":
            username = user_input[user_input.find(" ") + 1:]
            set_username(sock, username)

        if user_input[1:] == "exit":
            disconnect(sock)

    else:
        message_data = bytes(user_input, encoding='utf-8')
        util.send_message(sock, 
            message_data, 
            util.MessageType.TEXT)


def receive_message(sock, callback, fp):
    # get old chat logs
    util.send_message(sock, b'New connection', 
        util.MessageType.REQUEST_LOGS)

    while True:
        message = util.receive_message(sock) 
        if message is None:
            break

        if message.type == util.MessageType.KA_ENQ:
            util.send_message(sock, chr(6).encode("utf-8"), 
                util.MessageType.KA_ACK)
            continue

        # append to a file and somehow get the tk window to print it
        serial_message = str(message.data, encoding='utf-8')
        serial_message += '\n'

        # is it a server chat log or a recently received message?
        if serial_message[0] != chr(31):  # chat log entries start with US
            # print time if a normal message
            serial_message = time.strftime("%H:%m:%S") + ": " + serial_message
        else:
            # remove the US
            serial_message = serial_message[1:]

        log_lock.acquire()
        fp.write(serial_message.encode("utf-8"))
        log_lock.release()
        callback()

    print("receive_message: exiting")

def geometry_string(window, width, height):
    ws = window.winfo_screenwidth()
    hs = window.winfo_screenheight()
    x = (ws/2) - (width/2)
    y = (hs/2) - (height/2)
    return '%dx%d+%d+%d' % (width, height, x, y)

def client_tk():
    def new_connection():
        w_new_conn = tk.Toplevel(window)
        w_new_conn.title("New connection")
        w_new_conn.geometry(geometry_string(w_new_conn, 300, 100))

        var_ip = tk.StringVar()
        var_ip.set("IP: ")
        ip_entry_label = tk.Label(w_new_conn, textvariable=var_ip)
        ip_entry = tk.Entry(w_new_conn, width=17)
        ip_entry_label.grid(column=0, row=0)
        ip_entry.grid(column=1, row=0)

        var_port = tk.StringVar()
        var_port.set("Port: ")
        port_entry_label = tk.Label(w_new_conn, textvariable=var_port)
        port_entry = tk.Entry(w_new_conn, width=6)
        port_entry_label.grid(column=0, row=1)
        port_entry.grid(column=1, row=1)

        def enter_new_conn(event):
            new_conn(ip_entry.get(), port_entry.get())

        ip_entry.bind('<Return>', enter_new_conn)
        port_entry.bind('<Return>', enter_new_conn)

        button = tk.Button(w_new_conn, text="Connect",
            command=lambda: new_conn(ip_entry.get(), port_entry.get()))
        button.grid(column=1, row=2)

    def new_conn(ip, addr):
        # create and assign socket here
        print(ip, addr)
        # begin the receiver thread

    def send_message_tkinter(event):
        user_input = compose.get()
        compose.delete(0, tk.END)

        send_message(sock, user_input)

    # temp file for chat log
    fp = tempfile.TemporaryFile()

    # set up the window
    window = tk.Tk()
    window.title("GOMBU v1.0d")
    window.geometry(geometry_string(window, 640, 480))
    window.resizable(False,False)

    # menu bar
    menu_bar = tk.Menu(window)
    menu_file = tk.Menu(menu_bar, tearoff=0)
    menu_file.add_command(label="New connection...",
        command=new_connection)
    menu_bar.add_cascade(label="File", menu=menu_file)

    # a read-only area, large, to display inbox
    inbox = tk.Text(window, width=80, height=24)
    inbox.pack()
    inbox.config(state=tk.DISABLED)

    # compose area where message is sent
    compose = tk.Entry(window, width=64)
    compose.bind('<Return>', send_message_tkinter)
    compose.pack()

    window.config(menu=menu_bar)
    window.mainloop()
    
    sys.exit(0)


    
def client(addr, port):
    # temp file for chat log
    fp = tempfile.TemporaryFile()

    # set up the window
    window = tk.Tk()
    window.title("GOMBU v1.0d")
    window.geometry("640x480+50+50")
    window.resizable(False,False)

    # a read-only area, large, to display inbox
    inbox = tk.Text(window, width=80, height=24)
    inbox.pack()
    inbox.config(state=tk.DISABLED)

    # compose area where message is sent
    compose = tk.Entry(window, width=64)
    compose.pack()

    def fancy_bind(widget, callback):
        sequence = "<<RECEIVED>>"

        def send():
            # in receive thread
            widget.event_generate(sequence)

        widget.bind(sequence, callback)

        return send

    def update_received_text(event):
        log_lock.acquire()
        fp.seek(0)
        inbox.config(state=tk.NORMAL)
        inbox.delete('1.0', tk.END)
        idx_line = 1;
        for line in fp:
            line_utf = line.decode("utf-8")

            # calculate the position
            pos = str(idx_line) + '.' + str(0)
            inbox.insert(pos, line_utf)
            idx_line += 1

        inbox.config(state=tk.DISABLED)
        inbox.see("end")
        log_lock.release()

    send_receive_event = fancy_bind(window, update_received_text)

    # set up network
    sock = socket.create_connection((addr, port)) 
    sock.setblocking(True)
    f_connected = True

    def send_message_tkinter(event):
        user_input = compose.get()
        compose.delete(0, tk.END)

        send_message(sock, user_input)
    compose.bind('<Return>', send_message_tkinter)
    
    recv_t = threading.Thread(target=receive_message, 
        args=(sock, send_receive_event, fp),
        daemon=True)
    recv_t.start()

    window.mainloop()

    if f_connected:
        disconnect(sock)

    recv_t.join()
    fp.close()
    

def print_help():
    print("./client [server address] [server port]")
    sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        #print_help()
        client_tk()

    addr = sys.argv[1]
    try:
        port = int(sys.argv[2])
    except:
        print_help()

    client(addr, port)

    print("Exited.")
