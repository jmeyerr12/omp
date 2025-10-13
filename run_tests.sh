#!/usr/bin/env bash
set -euo pipefail

# ===========================
# Parâmetros
# ===========================
REPS=${REPS:-20}                          # repetições por (binário, entrada)
THREADS="${THREADS:-1 2 4 8 16}"          # lista de p
INPUT_GLOB="${INPUT_GLOB:-inputs/*.txt}"  # onde estão as entradas
CSV_FILE="resultados.csv"

SEQ_BIN=./shsup_seq_not_compat
OMP_BIN=./shsup_omp_not_compat
OMP_SERIAL_BIN=./shsup_omp_not_compat_serial

# ===========================
# Build (só compila se precisar)
# ===========================
if ! make -q all 2>/dev/null; then
  echo "[build] Compilando..."
  make -s all
fi

# ===========================
# Helpers de estatística (Welford em awk)
# Lê números (um por linha) e imprime "mean sd"
# ===========================
mean_sd_from_stream() {
  awk '
    { gsub(",","."); x=$1;
      n+=1; delta=x-mean; mean+=delta/n; m2+=delta*(x-mean);
    }
    END{
      if (n>1) { sd=sqrt(m2/(n-1)); } else { sd=0.0; }
      printf("%.9f %.9f\n", mean+0.0, sd+0.0);
    }'
}

# ===========================
# Medições (coletam streams e já devolvem mean/sd)
# ===========================

# Sequencial: última linha do stdout em cada repetição
measure_seq_mean_sd() {
  local infile="$1"
  (
    for ((r=1; r<=REPS; r++)); do
      "$SEQ_BIN" < "$infile" 2>/dev/null | tail -n1
    done
  ) | mean_sd_from_stream
}

# OMP serial-equivalente: 1ª linha do stderr (coluna 1=total, 3=seq_frac)
# Retorna: "mean_total sd_total mean_seqfrac sd_seqfrac"
measure_omp_serial_equiv_stats() {
  local infile="$1"
  local totals seqfracs
  totals="$(
    for ((r=1; r<=REPS; r++)); do
      "$OMP_SERIAL_BIN" < "$infile" 2>&1 >/dev/null | awk 'NR==1{print $1}'
    done
  )"
  seqfracs="$(
    for ((r=1; r<=REPS; r++)); do
      "$OMP_SERIAL_BIN" < "$infile" 2>&1 >/dev/null | awk 'NR==1{print $3}'
    done
  )"
  read -r mt st <<<"$(printf "%s\n" "$totals"  | mean_sd_from_stream)"
  read -r msf ssf <<<"$(printf "%s\n" "$seqfracs" | mean_sd_from_stream)"
  printf "%s %s %s %s\n" "$mt" "$st" "$msf" "$ssf"
}

# OMP com p threads: 1ª linha do stderr (col1=total, col3=seq_frac)
# Retorna: "mean_total sd_total mean_seqfrac sd_seqfrac"
measure_omp_p_stats() {
  local infile="$1" p="$2"
  local totals seqfracs
  totals="$(
    for ((r=1; r<=REPS; r++)); do
      OMP_NUM_THREADS="$p" OMP_PROC_BIND=TRUE \
      "$OMP_BIN" < "$infile" 2>&1 >/dev/null | awk 'NR==1{print $1}'
    done
  )"
  seqfracs="$(
    for ((r=1; r<=REPS; r++)); do
      OMP_NUM_THREADS="$p" OMP_PROC_BIND=TRUE \
      "$OMP_BIN" < "$infile" 2>&1 >/dev/null | awk 'NR==1{print $3}'
    done
  )"
  read -r mt st <<<"$(printf "%s\n" "$totals"  | mean_sd_from_stream)"
  read -r msf ssf <<<"$(printf "%s\n" "$seqfracs" | mean_sd_from_stream)"
  printf "%s %s %s %s\n" "$mt" "$st" "$msf" "$ssf"
}

# ===========================
# Cabeçalho + CSV
# ===========================
printf "\n==== Config ====\n"
echo "REPS     = $REPS"
echo "THREADS  = $THREADS"
echo "INPUTS   = $INPUT_GLOB"
printf "============\n\n"

printf "%-18s %-6s %-10s %-10s %-12s %-12s %-12s %-12s %-10s %-10s %-10s\n" \
       "input" "p" "T_seq" "T_seq_sd" "T_serEQ" "T_serEQ_sd" "T_omp" "T_omp_sd" "S_justo" "Eficiência" "seq_frac"
printf "%s\n" "---------------------------------------------------------------------------------------------------------------------"

echo "input,threads,T_seq,T_seq_sd,T_serEQ,T_serEQ_sd,T_omp,T_omp_sd,S_justo,Eficiência,seq_frac" > "$CSV_FILE"

# ===========================
# Loop (um arquivo por vez)
# ===========================
shopt -s nullglob
inputs=( $INPUT_GLOB )
if (( ${#inputs[@]} == 0 )); then
  echo "Nenhum arquivo casa com $INPUT_GLOB"
  exit 1
fi

for f in "${inputs[@]}"; do
  base="$(basename "$f")"

  # seq (mean, sd)
  read -r SEQ_MEAN SEQ_SD <<<"$(measure_seq_mean_sd "$f")"

  # serial-equivalente (mean, sd, seqfrac_mean, seqfrac_sd)
  read -r SER_MEAN SER_SD _ _ <<<"$(measure_omp_serial_equiv_stats "$f")"

  for p in $THREADS; do
    # omp p (mean, sd, seqfrac_mean, seqfrac_sd)
    read -r OMP_MEAN OMP_SD OMP_SEQFRAC_MEAN _ <<<"$(measure_omp_p_stats "$f" "$p")"

    # S_justo (base em T_serEQ) e Eficiência
    read -r S_JUSTO EFF_JUSTO <<<"$(
      awk -v ser="$SER_MEAN" -v omp="$OMP_MEAN" -v p="$p" 'BEGIN{
        s = (omp>0? ser/omp : 0);
        e = (p>0? s/p : 0);
        printf "%.9f %.9f\n", s, e;
      }'
    )"

    # Terminal
    printf "%-18s %-6s %-10.6f %-10.6f %-12.6f %-12.6f %-12.6f %-12.6f %-10.4f %-10.4f %-10.4f\n" \
      "$base" "$p" "$SEQ_MEAN" "$SEQ_SD" "$SER_MEAN" "$SER_SD" "$OMP_MEAN" "$OMP_SD" "$S_JUSTO" "$EFF_JUSTO" "$OMP_SEQFRAC_MEAN"

    # CSV
    echo "$base,$p,$SEQ_MEAN,$SEQ_SD,$SER_MEAN,$SER_SD,$OMP_MEAN,$OMP_SD,$S_JUSTO,$EFF_JUSTO,$OMP_SEQFRAC_MEAN" >> "$CSV_FILE"
  done
done

echo
echo "Resultados exportados para: $CSV_FILE"
echo
echo "Legenda: T_seq/T_seq_sd = média e desvio do sequencial;"
echo "         T_serEQ/T_serEQ_sd = média e desvio do serial-equivalente;"
echo "         T_omp/T_omp_sd = média e desvio do OMP(p);"
echo "         S_justo = T_serEQ/T_omp; Eficiência = S_justo/p; seq_frac = fração sequencial média."
