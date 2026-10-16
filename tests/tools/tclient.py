import socket, time, sys
# Connect to SIMH's multiplexer port after a delay, log in, and record what comes back.
time.sleep(float(sys.argv[1]))
s = socket.create_connection(("127.0.0.1", 1054))
s.settimeout(0.5)
out = open(sys.argv[2], "wb")
def pump(sec):
    end = time.time() + sec
    while time.time() < end:
        try:
            d = s.recv(4096)
            if not d: return
            out.write(d); out.flush()
        except socket.timeout:
            pass
pump(2)
s.sendall(b"HELLO-B001,RICK\r")
pump(6)
s.sendall(b"GET-TEN\r")
pump(4)
s.sendall(b"RUN\r")
pump(6)
s.sendall(b"BYE\r")
pump(4)
