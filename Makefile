# -----------------------------
# Compiladores
# -----------------------------
CC   = gcc
CXX  = g++

# -----------------------------
# Flags de compilação
# -----------------------------
CFLAGS   = -O3 -march=native -Wall -Wextra
CXXFLAGS = -O3 -std=c++11 -Wall -Wextra
OMPFLAGS = -fopenmp

# -----------------------------
# Alvos principais
# -----------------------------
all: shsup_seq shsup_omp shsup_seq_not_compat shsup_omp_not_compat input_gen

# Versão sequencial (professor original)
shsup_seq: shortest_superstring.cc
	$(CXX) $(CXXFLAGS) $< -o $@

# Versão paralela OpenMP (professor original)
shsup_omp: shortest_superstring_omp.cc
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) $< -o $@

# Versão sequencial corrigida (not_compat)
shsup_seq_not_compat: shortest_superstring_not_compat.cc
	$(CXX) $(CXXFLAGS) $< -o $@

# Versão paralela corrigida (not_compat)
shsup_omp_not_compat: shortest_superstring_omp_not_compat.cc
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) $< -o $@

# Gerador de entradas
input_gen: input-generator.cc
	$(CXX) $(CXXFLAGS) $< -o $@

# -----------------------------
# Limpeza
# -----------------------------
clean:
	rm -f shsup_seq shsup_omp shsup_seq_not_compat shsup_omp_not_compat input_gen
