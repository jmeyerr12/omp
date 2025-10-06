# Compiladores
CC   = gcc
CXX  = g++

# Flags de compilação
CFLAGS   = -O3 -march=native -Wall -Wextra
CXXFLAGS = -O3 -std=c++11 -Wall -Wextra
OMPFLAGS = -fopenmp

# Alvos padrão
all: ssp_seq ssp_omp shsup_seq shsup_omp input_gen

# ---- Versões em C ----
ssp_seq: ssp_seq.c
	$(CC) $(CFLAGS) $< -o $@

ssp_omp: ssp_omp.c
	$(CC) $(CFLAGS) $(OMPFLAGS) $< -o $@

# ---- Versões em C++ (shortest superstring) ----
shsup_seq: shortest_superstring.cc
	$(CXX) $(CXXFLAGS) $< -o $@

shsup_omp: shortest_superstring_omp.cc
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) $< -o $@

input_gen: input-generator.cc
	$(CXX) $(CXXFLAGS) $< -o $@

# ---- Limpeza ----
clean:
	rm -f ssp_seq ssp_omp shsup_seq shsup_omp input_gen
