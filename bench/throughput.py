#!/usr/bin/env python3
# =============================================================================
# throughput.py — misura il throughput del server al variare delle connessioni.
#
# Metrica principale: operazioni al secondo (ops/s), calcolata come:
#
#   ops/s = (N_thread × ITERS) / elapsed
#
# dove elapsed è il tempo passato dalla creazione del primo thread
# al join dell'ultimo che permette di misura il throughput dell'intero sistema.
# =============================================================================

import socket
import sys
import time
import threading

HOST = "127.0.0.1"
PORT = 5050

DATASET_0 = "promessi_sposi.txt"
DATASET_1 = "divina.txt"
OUT = "throughput_results.csv"

ITERS = 10000     # operazioni per connessione
T = [1, 2, 4, 8, 16]  # livelli di parallelismo da testare


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    return s

def cmd(sock, text):
    sock.sendall((text + "\r\n").encode())
    return sock.recv(8192)


# load_dataset — carica un file di testo nel database indicato.
def load_dataset(db, path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = [l.rstrip("\n").rstrip("\r") for l in f.readlines()]

    s = connect()
    cmd(s, f"SELECT {db}")
    n = 0
    for i, line in enumerate(lines):
        if not line.strip():
            continue
        cmd(s, f"SET k{i + 1} {line.strip()}")
        n += 1
    s.close()
    return n


# init_counters — inizializza N contatori a 0 nel database indicato.
def init_counters(db, n):
    s = connect()
    cmd(s, f"SELECT {db}")
    for i in range(1, n + 1):
        cmd(s, f"SET c{i} 0")
    s.close()


# =============================================================================
# bench_worker — esegue ITERS operazioni su una connessione.
#
# Ogni worker:
#   1. Apre una connessione TCP dedicata
#   2. Seleziona il database db = tid % 2 (pari → db0, dispari → db1)
#   3. Esegue ITERS comandi consecutivi (GET, SET, INCR, DEL)
#   4. Registra il proprio tempo di esecuzione in results[tid]
#
# La chiave usata ruota con (i % nkeys) + 1, distribuendo il carico
# uniformemente su tutte le chiavi del dataset. Per esempio con
# nkeys=14339 e ITERS=10000, il worker tocca le chiavi k1, k2, ...,
# k10000 esattamente una volta ciascuna.
# =============================================================================
def bench_worker(results, tid, cmd_name, nkeys, iters):
    s = connect()
    db = tid % 2
    cmd(s, f"SELECT {db}")

    t0 = time.time()
    if cmd_name == "GET":
        for i in range(iters):
            cmd(s, f"GET k{(i % nkeys) + 1}") # la prima chiave parte da 1 (+1)
    elif cmd_name == "SET":
        for i in range(iters):
            cmd(s, f"SET k{(i % nkeys) + 1} v{i}")
    elif cmd_name == "INCR":
        for i in range(iters):
            cmd(s, f"INCR c{(i % nkeys) + 1}")
    elif cmd_name == "DEL":
        for i in range(iters):
            cmd(s, f"DEL k{(i % nkeys) + 1}")
    t1 = time.time()
    results[tid] = t1 - t0
    s.close()


# bench — coordina n_threads worker e calcola il throughput aggregato.
def bench(cmd_name, n_threads, nkeys, iters):
    threads = [] # Per fare join
    results = [0.0] * n_threads

    t0 = time.time()
    for tid in range(n_threads):
        t = threading.Thread(target=bench_worker,
                             args=(results, tid, cmd_name, nkeys, iters))
        t.start()
        threads.append(t)

    for t in threads:
        t.join()
    elapsed = time.time() - t0 # Tempo aggregato di tutti i threads

    total_ops = n_threads * iters
    return elapsed, total_ops, total_ops / elapsed


# ============================================================================
# 1. Apre il file .csv
# 2. Inizializza i counters
# 3. Per ogni comando e per ogni numero di Thread esegue bench()
# ============================================================================
if __name__ == "__main__":
    n0 = load_dataset(0, DATASET_0)
    n1 = load_dataset(1, DATASET_1)
    nkeys = max(n0, n1)
    init_counters(0, nkeys)
    init_counters(1, nkeys)
    print(f"# db0: {n0} keys ({DATASET_0}), db1: {n1} keys ({DATASET_1})")
    sys.stdout.flush()

    with open(OUT, "w") as f:
        f.write(f"# db0={n0} ({DATASET_0}), db1={n1} ({DATASET_1})\n")
        row = "command,connections,iterations,total_time_sec,total_operations,operations_per_sec"
        print(row)
        f.write(row + "\n")

        for cmd_name in ["GET", "SET", "INCR", "DEL"]:
            for threads in T:
                elapsed, total, rate = bench(cmd_name, threads, nkeys, ITERS)
                row = f"{cmd_name},{threads},{ITERS},{elapsed:.3f},{total},{rate:.0f}"
                print(row)
                f.write(row + "\n")
                f.flush()
                sys.stdout.flush()
