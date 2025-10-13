# -----------------------------
# Compiladores
# -----------------------------
CC   = gcc
CXX  = g++

# -----------------------------
# Flags de compilação (iguais para todos)
# -----------------------------
BASECXXFLAGS = -O3 -march=native -DNDEBUG -Wall -Wextra -std=c++11 -pipe
OMPFLAGS    = -fopenmp

# -----------------------------
# Alvos principais
# -----------------------------
# Ajuste aqui os alvos que você realmente usa no trabalho
all: shsup_seq_not_compat shsup_omp_not_compat shsup_omp_not_compat_serial

# Versão sequencial (not_compat)
shsup_seq_not_compat: shortest_superstring_not_compat.cc
	$(CXX) $(BASECXXFLAGS) $< -o $@

# Versão paralela OpenMP (not_compat)
shsup_omp_not_compat: shortest_superstring_omp_not_compat.cc
	$(CXX) $(BASECXXFLAGS) $(OMPFLAGS) $< -o $@

# Versão "serial-equivalente" da OMP (compila o mesmo código SEM -fopenmp)
shsup_omp_not_compat_serial: shortest_superstring_omp_not_compat.cc
	$(CXX) $(BASECXXFLAGS) $< -o $@

# (Opcional) Gerador de entradas, se você utilizar
input_gen: input-generator.cc
	$(CXX) $(BASECXXFLAGS) $< -o $@

# -----------------------------
# Limpeza
# -----------------------------
clean:
	rm -f shsup_seq_not_compat shsup_omp_not_compat shsup_omp_not_compat_serial input_gen
