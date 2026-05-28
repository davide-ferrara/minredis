#!/usr/bin/env python3
# Test: invio di un comando spezzato su piu' send().
# Simula pacchetti TCP frammentati: il server accumula i byte
# nel querybuf finche' non trova \r\n, poi esegue il comando.
import socket, time

def send(s, msg, t):
    s.send(msg)
    print("Inviati ", len(msg), "byte")
    time.sleep(t)

s = socket.socket()
s.connect(('127.0.0.1', 5050))

send(s, b'SET ', 0.5)
send(s, b'no', 0.5)
send(s, b'me D', 0.5)
send(s, b'avide\r\n', 0.5)

send(s, b'GET ', 0.5)
send(s, b'nome', 0.5)
send(s, b'\r\n', 0.5)

print(s.recv(4096).decode())
