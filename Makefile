CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra 
OMPFLAGS = -fopenmp

all: ssp_seq ssp_omp

ssp_seq: ssp_seq.c
	$(CC) $(CFLAGS) $< -o $@

ssp_omp: ssp_omp.c
	$(CC) $(CFLAGS) $(OMPFLAGS) $< -o $@

clean:
	rm -f ssp_seq ssp_omp
