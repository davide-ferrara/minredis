#!/usr/bin/env python3
# =============================================================================
# race.py — test di race condition con INCR concorrenti.
#
# Metrica: "aggiornamenti persi", ovvero la differenza fra il valore atteso di
# un contatore e il valore letto dopo N thread concorrenti che lo incrementano.
#
#   expected = N_thread * ITERS
#   diff     = atteso - risultato
#
# Se diff == 0 su tutti i counter, l'operazione composta GET+SET dentro INCR
# è atomica grazie al wrlock per-bucket. Se diff > 0 significa che almeno un
# incremento è andato perso (race condition classica read-modify-write).
#
# Setup: crea N counter a 0, lancia T thread, ogni thread esegue ITERS × N
# INCR su tutti i counter. Alla fine somma i valori e confronta con expected.
# =============================================================================

import socket
import sys
import threading

HOST = "127.0.0.1"
PORT = 5050

# quanti contatori creare (distribuiti su bucket diversi)
N = 10
# quanti incrementi esegue ogni thread su ogni contatore
ITERS = 10000
# quanti thread paralleli testare
T = [1, 2, 4, 8, 16]
OUT = "race_results.csv"


# Apre una connessione TCP verso il server.
def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    return s


# Ivia un comando testuale (protocollo inline CRLF) e riceve risposta.
def cmd(sock, text):
    sock.sendall((text + "\r\n").encode())
    return sock.recv(8192)


# Estrae il valore intero da una risposta bulk RESP.
# Esempio di risposta a GET:
#   "$2\r\n42\r\n"  -->  42
def get_value(sock, key):
    r = cmd(sock, f"GET {key}")
    try:
        body = r.decode().strip().split("\r\n")
        return int(body[1])
    except Exception:
        return None


# incr_worker — funzione eseguita da ogni thread del test.
def incr_worker(tid):
    s = connect()
    for _ in range(ITERS):
        for i in range(N):
            cmd(s, f"INCR c{i}")
    s.close()


# run_race — esegue il test per un dato numero di thread.
#
# 1. Inizializza tutti i counter a 0 (SET da una connessione)
# 2. Lancia n_threads worker in parallelo
# 3. Aspetta che finiscano tutti trmaite join
# 4. Legge tutti i counter e calcola expected vs actual
def run_race(n_threads):
    # azzera i counter prima del test
    s = connect()
    for i in range(N):
        cmd(s, f"SET c{i} 0")
    s.close()

    # lancia i thread concorrenti
    threads = []
    for tid in range(n_threads):
        t = threading.Thread(target=incr_worker, args=(tid,))
        t.start()
        threads.append(t)

    # aspetta che finiscano tutti
    for t in threads:
        t.join()

    # verifica il risultato
    s = connect()
    actual = 0
    ok = True
    for i in range(N):
        v = get_value(s, f"c{i}")
        if v is None:
            ok = False
        elif v != ITERS * n_threads:
            ok = False
        if v is not None:
            actual += v
    s.close()

    expected = ITERS * n_threads * N
    return expected, actual, ok


# Stampa CSV su stdout e su file.
if __name__ == "__main__":
    print("threads,expected,actual,diff,ok")
    sys.stdout.flush()

    with open(OUT, "w") as f:
        f.write("threads,expected,actual,diff,ok\n")

        for threads in T:
            expected, actual, ok = run_race(threads)
            diff = expected - actual
            row = f"{threads},{expected},{actual},{diff},{'OK' if ok else 'RACE'}"
            print(row)
            f.write(row + "\n")
            f.flush()
            sys.stdout.flush()
