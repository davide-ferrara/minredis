#!/usr/bin/env python3
# =============================================================================
# plot.py — genera i grafici dai CSV dei benchmark.
#
# Tre grafici, uno per file PNG:
#
#   1. tput_totale.png  — throughput aggregato (ops/s totali).
#      Asse X: numero di connessioni.
#      Asse Y: migliaia di operazioni al secondo (ops/s / 1000).
#      Mostra come cresce la capacità del server all'aumentare dei client.
#      Una curva per ogni comando (GET, SET, INCR).
#
#   2. tput_perconn.png — throughput per connessione (ops/s / thread).
#      Asse X: numero di connessioni.
#      Asse Y: migliaia di ops/s per singola connessione.
#      Mostra se il server scala linearmente o se c'è degrado
#      dovuto a contesa sui lock (scalabilità di Gustafson).
#      Una connessione da sola fa sempre più ops/s di una connessione
#      in un gruppo numeroso, perché non condivide il canale.
#
#   3. race.png — aggiornamenti persi nel test di race condition.
#      Asse X: thread concorrenti.
#      Asse Y: differenza expected - actual.
#      Barre verdi se diff == 0 (nessuna race), rosse se c'è perdita.
#      Con il wrlock per-bucket ci aspettiamo sempre verde.
#
# Input: throughput_results.csv, race_results.csv nella stessa directory.
# Output: 3 file PNG salvati nella stessa directory.
# =============================================================================

import csv
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
TPUT_CSV = os.path.join(BENCH_DIR, "throughput_results.csv")
RACE_CSV = os.path.join(BENCH_DIR, "race_results.csv")

# file di output
TPUT_TOT = os.path.join(BENCH_DIR, "tput_totale.png")
TPUT_CONN = os.path.join(BENCH_DIR, "tput_perconn.png")
RACE_PNG = os.path.join(BENCH_DIR, "race.png")


# =============================================================================
# load_throughput — carica il CSV del throughput in un dizionario.
#
# Formato atteso:
#   command,connections,iterations,total_time_sec,total_operations,operations_per_sec
#
# Restituisce: { "GET": [(1, 56300), (2, 102400), ...], "SET": [...], ... }
# dove ogni tupla è (connessioni, ops_s).
# =============================================================================
def load_throughput(path):
    cmds = {}
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                continue
            row = line.strip().split(",")
            if row[0] == "command":
                continue
            cmd, conns, _, _, _, ops_s = row
            cmds.setdefault(cmd, []).append((int(conns), float(ops_s)))
    return cmds


# =============================================================================
# load_race — carica il CSV del test race.
#
# Formato: threads,expected,actual,diff,ok
# Restituisce: (threads_list, diffs_list, oks_list).
# =============================================================================
def load_race(path):
    threads, diffs, oks = [], [], []
    with open(path) as f:
        for line in f:
            if line.startswith("threads"):
                continue
            t, _, _, diff, ok = line.strip().split(",")
            threads.append(int(t))
            diffs.append(int(diff))
            oks.append(ok == "OK")
    return threads, diffs, oks


# =============================================================================
# plot_tput_totale — throughput aggregato.
#
# Mostra le operazioni totali al secondo su scala lineare.
# I punti sono uniti da linea continua con marker tondo.
# Tre curve sovrapposte: GET, SET, INCR.
# =============================================================================
def plot_tput_totale(throughput):
    fig, ax = plt.subplots(figsize=(6, 4))
    for cmd, pts in throughput.items():
        pts.sort()
        xs = [p[0] for p in pts]
        ys = [p[1] / 1000 for p in pts]
        ax.plot(xs, ys, marker="o", label=cmd)
    ax.set_xlabel("connessioni")
    ax.set_ylabel("ops/s (×1000)")
    ax.set_title("throughput totale")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(TPUT_TOT, dpi=150)
    plt.close(fig)
    print(f"saved {TPUT_TOT}")


# =============================================================================
# plot_tput_perconn — throughput per singola connessione.
#
# Formula: ops_s_per_conn = ops_s_totali / numero_connessioni.
# Se il sistema scala linearmente, questa curva è piatta (ogni connessione
# contribuisce in egual misura). Se cala, c'è contesa su risorse condivise
# (lock, CPU, I/O).
#
# Usa marker quadrato e linea tratteggiata per distinguerlo dal grafico
# del throughput totale.
# =============================================================================
def plot_tput_perconn(throughput):
    fig, ax = plt.subplots(figsize=(6, 4))
    for cmd, pts in throughput.items():
        pts.sort()
        xs = [p[0] for p in pts]
        ys = [p[1] / p[0] / 1000 for p in pts]
        ax.plot(xs, ys, marker="s", linestyle="--", label=cmd)
    ax.set_xlabel("connessioni")
    ax.set_ylabel("ops/s per conn (×1000)")
    ax.set_title("throughput per connessione")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(TPUT_CONN, dpi=150)
    plt.close(fig)
    print(f"saved {TPUT_CONN}")


# =============================================================================
# plot_race — grafico a barre degli aggiornamenti persi.
#
# Verde  (#2ecc71) = nessuna race condition (diff == 0)
# Rosso  (#e74c3c) = aggiornamenti persi (diff > 0)
#
# L'altezza della barra è la differenza expected - actual.
# Dovremmo vedere sempre barre a zero (verdi) grazie al locking per-bucket.
# =============================================================================
def plot_race(race):
    threads, diffs, oks = race
    fig, ax = plt.subplots(figsize=(6, 4))
    colors = ["#2ecc71" if ok else "#e74c3c" for ok in oks]
    ax.bar([str(t) for t in threads], diffs, color=colors, edgecolor="#333")
    ax.set_xlabel("thread concorrenti")
    ax.set_ylabel("aggiornamenti persi")
    ax.set_title("race condition (atteso - risultato)")
    ax.grid(True, alpha=0.3, axis="y")
    fig.tight_layout()
    fig.savefig(RACE_PNG, dpi=150)
    plt.close(fig)
    print(f"saved {RACE_PNG}")


# =============================================================================
# Main: carica i CSV (se presenti) e genera i 3 grafici.
# =============================================================================
if __name__ == "__main__":
    if not os.path.exists(TPUT_CSV):
        print(f"{TPUT_CSV} non trovato", file=__import__("sys").stderr)
    if not os.path.exists(RACE_CSV):
        print(f"{RACE_CSV} non trovato", file=__import__("sys").stderr)

    if os.path.exists(TPUT_CSV) and os.path.exists(RACE_CSV):
        tput = load_throughput(TPUT_CSV)
        threads, diffs, oks = load_race(RACE_CSV)
        plot_tput_totale(tput)
        plot_tput_perconn(tput)
        plot_race((threads, diffs, oks))
