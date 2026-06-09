# Redis Minimale

Key-value store in-memory, protocollo RESP testuale, thread-per-client.

## Compilazione

```
make                    # server
make debug              # server con simboli di debug
make memory             # server con valgrind
```

## Esecuzione

```
./minredis                           # default porta 5050
./minredis --port 7000 --debug       # porta personalizzata + log debug
./minredis --restore dump.minredis   # ripristino da snapshot
```

## Test

```
make test                     # compila i binari di test
make run_tests                # compila ed esegue tutti i test
uv run bench/bench.py         # benchmark (race + throughput + plot)
uv run bench/net_latency.py   # test frammentazione TCP
```

## Comandi

```
SET nome Davide   -> +OK
GET nome          -> $6\r\nDavide
DEL nome          -> :1
INCR contatore    -> :42
SELECT 0          -> +OK
INFO              -> statistiche server
SAVE              -> salva snapshot
LOAD              -> carica snapshot
QUIT              -> chiude connessione
```
