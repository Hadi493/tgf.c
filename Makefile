CC = cc
CFLAGS = -Wall -Wextra -std=c11 -I./include -I./vendor -O3 -D_GNU_SOURCE -D_DEFAULT_SOURCE
LDFLAGS = -luv -lsqlite3 -ltdjson -lm -lpthread

SRCS = src/main.c src/storage.c src/handlers.c src/network.c src/config.c src/utils.c \
       vendor/cJSON.c vendor/toml.c vendor/sha256.c
OBJS = $(SRCS:.c=.o)
TARGET = tgf

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
