CC = gcc
CFLAGS = -Wall -Wextra -g -pthread

TARGETS = dispatcher ingester processor reporter
SRC_DIR = src

all: $(TARGETS)

dispatcher: $(SRC_DIR)/dispatcher.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ingester: $(SRC_DIR)/ingester.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

processor: $(SRC_DIR)/processor.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

reporter: $(SRC_DIR)/reporter.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	rm -rf logs/*