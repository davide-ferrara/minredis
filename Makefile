CC      = gcc
FLAGS   = -DLOG_USE_COLOR -g -Wall -Wextra -O2
LIBS    = -lpthread

SRC     = cmd.c parser.c strbuf.c hash.c log.c server.c reply.c serializer.c
HDR     = cmd.h parser.h strbuf.h hash.h log.h server.h reply.h serializer.h

BIN     = bin

all: minredis test_hash test_parser test_strbuf test_serializer

$(BIN):
	mkdir -p $(BIN)

minredis: $(BIN) minredis.c $(SRC) $(HDR)
	$(CC) $(FLAGS) -o $(BIN)/$@ minredis.c $(SRC) $(LIBS)

# Con ThreadSanitizer
tsan: $(BIN) minredis.c $(SRC) $(HDR)
	$(CC) $(FLAGS) -fsanitize=thread -o $(BIN)/minredis-tsan minredis.c $(SRC) $(LIBS)

test_hash: $(BIN) test_hash.c hash.c hash.h log.c log.h
	$(CC) $(FLAGS) -o $(BIN)/$@ test_hash.c hash.c log.c -lpthread

test_parser: $(BIN) test_parser.c $(SRC) $(HDR)
	$(CC) $(FLAGS) -o $(BIN)/$@ test_parser.c $(SRC) $(LIBS)

test_strbuf: $(BIN) test_strbuf.c strbuf.c strbuf.h
	$(CC) $(FLAGS) -o $(BIN)/$@ test_strbuf.c strbuf.c

test_serializer: $(BIN) test_serializer.c $(SRC) $(HDR)
	$(CC) $(FLAGS) -o $(BIN)/$@ test_serializer.c $(SRC) $(LIBS)

test: test_parser test_hash test_serializer test_strbuf
	for t in test_parser test_hash test_serializer test_strbuf; do \
		$(BIN)/$$t; \
	done

run: minredis
	./$(BIN)/minredis

clean:
	rm -f $(BIN)/*

clean_tests:
	rm -f $(BIN)/test_hash $(BIN)/test_parser $(BIN)/test_serializer $(BIN)/test_strbuf

memory:
	valgrind --leak-check=full --show-leak-kinds=all $(BIN)/minredis

.PHONY: all test clean clean_tests test_serializer
