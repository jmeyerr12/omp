CC=gcc
CFLAGS=-O3 -march=native -fopenmp -pipe -Wall -Wextra

all: ssp

ssp: ssp.c
	$(CC) $(CFLAGS) $< -o $@
	
clean:
	rm -f ssp
