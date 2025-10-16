# -----------------------------
# Compiladores
# -----------------------------
CC   = gcc
CXX  = g++

# -----------------------------
# Flags de compilação (iguais para todos)
# -----------------------------
BASECXXFLAGS = -O3 -march=native -DNDEBUG -Wall -Wextra -std=c++11
OMPFLAGS    = -fopenmp

# -----------------------------
# Alvos principais
# -----------------------------
# Ajuste aqui os alvos que você realmente usa no trabalho
all: shsup_omp_not_compat shsup_omp_not_compat_serial shsup_sequential shsup_paralel

# Versão paralela OpenMP (not_compat)
shsup_omp_not_compat: shortest_superstring_omp_not_compat.cc
	$(CXX) $(BASECXXFLAGS) $(OMPFLAGS) $< -o $@

# Versão "serial-equivalente" da OMP (compila o mesmo código SEM -fopenmp)
shsup_omp_not_compat_serial: shortest_superstring_omp_not_compat.cc
	$(CXX) $(BASECXXFLAGS) $< -o $@

# Versão totalmente sequencial
shsup_sequential: shortest_superstring_paralel.cc
	$(CXX) $(BASECXXFLAGS) $< -o $@

# Versão paralela principal (o novo código que você acabou de implementar)
shsup_paralel: shortest_superstring_paralel.cc
	$(CXX) $(BASECXXFLAGS) $(OMPFLAGS) $< -o $@

# (Opcional) Gerador de entradas
input_gen: input-generator.cc
	$(CXX) $(BASECXXFLAGS) $< -o $@

# -----------------------------
# Limpeza
# -----------------------------
clean:
	rm -f shsup_omp_not_compat shsup_omp_not_compat_serial shsup_sequential shsup_paralel input_gen
