# Redis Minimale

Key-value store in-memory, protocollo RESP testuale, thread-per-client.

## Compilazione

```
make                    # server + test
make tsan               # server con ThreadSanitizer
```

## Esecuzione

```
./bin/minredis                      # default porta 5050
./bin/minredis --port 7000 --debug  # porta personalizzata + log debug
./bin/minredis --restore dump.rdb   # ripristino da snapshot
```

## Test

```
make test                           # unit test
uv run bench/bench.py               # benchmark (race + throughput + plot)
uv run bench/net_latency.py         # test frammentazione TCP
valgrind --leak-check=full ./bin/minredis   # memory leak check
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
