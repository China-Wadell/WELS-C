.RECIPEPREFIX = >
CC = gcc
CFLAGS = -ffreestanding -fno-stack-protector -fno-pie -c

all: wels_rt.o

wels_rt.o: wels_rt.c
> $(CC) $(CFLAGS) wels_rt.c -o wels_rt.o

clean:
> rm -f wels_rt.o

.PHONY: all clean
