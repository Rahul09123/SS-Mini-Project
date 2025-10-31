CC = gcc
CFLAGS = -Wall -Wextra -pthread
SRCS = server.c \
       src/file_helpers.c \
       src/transactions.c \
       src/feedback.c \
       src/loans.c \
       src/menus.c \
       utils/utils.c

OBJS = $(SRCS:.c=.o)

all: server

server: $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o server

clean:
	rm -f server $(OBJS)

.PHONY: all clean
