CC        = gcc
CFLAGS    = -DLOG_USE_COLOR -D_GNU_SOURCE -std=c11 -Wall -Wextra -O2
CPPFLAGS  = -Iinclude -lpthread
TARGET    = minredis
BUILD     = build

# Tutti i file .c tranne minredis.c (che contiene il main) e i file di test (che contengono i loro main)
CORE_SRCS = $(filter-out src/minredis.c src/test_%.c,$(wildcard src/*.c))
# Sostituisco a ogni .c di CORE_SRCS .o, ottengo i nomi dei file oggetto
CORE_OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRCS))
# Nomi dei file .d (diepndeze degli header) ottenuti alla stessa maniera
DEPS      = $(patsubst src/%.c,$(BUILD)/%.d,$(wildcard src/*.c))
TEST_SRCS = $(wildcard src/test_*.c)

# Evitiamo i confilitti con il filesystem, ci fosse un file chiamato all make direbbe "is up to date"
.PHONY: all clean install uninstall debug test run_tests

all: $(TARGET)

# $^ espannde innome dipendeze, $@ espande in nome del target
$(TARGET): $(BUILD)/minredis.o $(CORE_OBJS)
	$(CC) $^ -o $@

# -MMD genera il file .d con le dipendenze dagli header, -MP aggiunge target vuoti per evitare errori se un header viene eliminato
# -c compila soltanto, crea il file `.o`
# $< prima dipendeza
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

# | questo simbolo fa in modo che se la cartella build non esiste viene creata (order-only prerequisite)
$(BUILD):
	mkdir -p $@

$(BUILD)/test_%: $(BUILD)/test_%.o $(CORE_OBJS)
	$(CC) $^ -o $@ $(CPPFLAGS)

# patsubst genera i nomi delle dipendeze e chiama il comando sopra
test: $(patsubst src/%.c,$(BUILD)/%,$(TEST_SRCS))

# Piccolo script bash se vogliamo per eseguire i test in serie
run_tests: test
	@for t in $(patsubst src/%.c,$(BUILD)/%,$(TEST_SRCS)); do \
		echo "=== $$t ==="; \
		./$$t || exit 1; \
	done

install: $(TARGET)
	@echo "Installing minredis..."
	cp $(TARGET) /usr/bin/$(TARGET)
	@echo "Installation complete!"

uninstall:
	@echo "Uninstalling minredis..."
	rm -f /usr/bin/$(TARGET)
	@echo "Uninstall complete!"

clean:
	rm -rf $(BUILD) $(TARGET)

# Aggiunge i flag di debug solo quando si esegue `make debug`
debug: CFLAGS += -g -DDEBUG
debug: all

# impedisce a make di cancellare tutti i file intermedi come i `.o`
.SECONDARY:

# Carica le dipendenze dei file .o generati da -MMD e -MP in modo che se modifico un `.h` make conosce la dipendeza di esso col `.o`
-include $(DEPS)
