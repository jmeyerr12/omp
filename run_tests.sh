#!/usr/bin/env bash
set -euo pipefail

# ===========================
# Parâmetros
# ===========================
REPS=${REPS:-20}                          # repetições por (binário, entrada)
THREADS="${THREADS:-1 2 4 8 16}"          # lista de p
INPUT_GLOB="${INPUT_GLOB:-inputs/*.txt}"  # onde estão as entradas

CSV_MAIN="resultados.csv"                 # CSV legado (compatível)
CSV_EXT="resultados_extendidos.csv"       # CSV estendido (extra)

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
# Helpers de estatística (awk com Welford)
# Retorna: "mean sd" (em seg)
# ===========================
mean_sd_from_stream() {
  # Lê números (um por linha) de stdin e imprime "mean sd"
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
# Medições
# ===========================

# Sequencial: coleta REPS tempos (última linha do stdout) -> mean, sd
measure_seq_mean_sd() {
  local infile="$1"
  (
    for ((r=1; r<=REPS; r++)); do
      "$SEQ_BIN" < "$infile" 2>/dev/null | tail -n1
    done
  ) | mean_sd_from_stream
}

# OMP serial-equivalente: coleta REPS da 1ª linha do stderr (total, parallel, seq_frac)
# Retorna: "mean_total sd_total mean_seqfrac"
measure_omp_serial_equiv_stats() {
  local infile="$1"
  # total times em um stream para mean/sd; e seq_frac em stream separado para mean
  # (parallel não é necessário para as métricas finais)
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
  # mean/sd de total
  read -r mt st <<<"$(printf "%s\n" "$totals" | mean_sd_from_stream)"
  # mean de seq_frac
  msf="$(printf "%s\n" "$seqfracs" | awk '{gsub(",","."); s+=$1} END{if(NR) printf "%.9f\n", s/NR; else print "1.0"}')"
  printf "%s %s %s\n" "$mt" "$st" "$msf"
}

# OMP com p threads: coleta REPS (total, parallel, seq_frac)
# Retorna: "mean_total sd_total mean_seqfrac"
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
  read -r mt st <<<"$(printf "%s\n" "$totals" | mean_sd_from_stream)"
  msf="$(printf "%s\n" "$seqfracs" | awk '{gsub(",","."); s+=$1} END{if(NR) printf "%.9f\n", s/NR; else print "1.0"}')"
  printf "%s %s %s\n" "$mt" "$st" "$msf"
}

# ===========================
# Cabeçalho + CSVs
# ===========================
printf "\n==== Config ====\n"
echo "REPS     = $REPS"
echo "THREADS  = $THREADS"
echo "INPUTS   = $INPUT_GLOB"
printf "============\n\n"

printf "%-18s %-6s %-10s %-12s %-12s %-10s %-10s %-10s\n" \
       "input" "p" "T_seq" "T_serEQ" "T_omp" "S_justo" "Eficiência" "seq_frac"
printf "%s\n" "---------------------------------------------------------------------------------------------"

# CSV legado (compatível com seu cabeçalho antigo)
echo "input,threads,seq_mean_s,seq_sd_s,omp_mean_s,omp_sd_s,speedup,efficiency" > "$CSV_MAIN"
# CSV estendido (extra)
echo "input,threads,seq_mean_s,seq_sd_s,serEQ_mean_s,serEQ_sd_s,omp_mean_s,omp_sd_s,S_justo,Eficiência,seq_frac_mean" > "$CSV_EXT"

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

  # seq: mean/sd
  read -r SEQ_MEAN SEQ_SD <<<"$(measure_seq_mean_sd "$f")"

  # serial-equivalente: mean/sd + seq_frac_mean
  read -r SER_MEAN SER_SD SER_SEQFRAC_MEAN <<<"$(measure_omp_serial_equiv_stats "$f")"

  for p in $THREADS; do
    # omp(p): mean/sd + seq_frac_mean
    read -r OMP_MEAN OMP_SD OMP_SEQFRAC_MEAN <<<"$(measure_omp_p_stats "$f" "$p")"

    # speedups/eficiências
    # - legado: speedup vs seq clássico (compatível com seu CSV antigo)
    # - justo:  S_justo vs serial-equivalente (para análise Amdahl/relatório)
    read -r SPEEDUP_LEGACY EFF_LEGACY S_JUSTO EFF_JUSTO <<<"$(
      awk -v seq="$SEQ_MEAN" -v omp="$OMP_MEAN" -v ser="$SER_MEAN" -v p="$p" 'BEGIN{
        sp_legacy = (omp>0? seq/omp : 0);
        eff_legacy = (p>0? sp_legacy/p : 0);
        s_justo = (omp>0? ser/omp : 0);
        eff_justo = (p>0? s_justo/p : 0);
        printf "%.9f %.9f %.9f %.9f\n", sp_legacy, eff_legacy, s_justo, eff_justo;
      }'
    )"

    # Print na tabela do terminal (mostro S_justo/Eff_justo e seq_frac média da OMP)
    printf "%-18s %-6s %-10.6f %-12.6f %-12.6f %-10.4f %-10.4f %-10.4f\n" \
      "$base" "$p" "$SEQ_MEAN" "$SER_MEAN" "$OMP_MEAN" "$S_JUSTO" "$EFF_JUSTO" "$OMP_SEQFRAC_MEAN"

    # CSV legado (compatível com seu cabeçalho antigo)
    echo "$base,$p,$SEQ_MEAN,$SEQ_SD,$OMP_MEAN,$OMP_SD,$SPEEDUP_LEGACY,$EFF_LEGACY" >> "$CSV_MAIN"

    # CSV estendido (extra)
    echo "$base,$p,$SEQ_MEAN,$SEQ_SD,$SER_MEAN,$SER_SD,$OMP_MEAN,$OMP_SD,$S_JUSTO,$EFF_JUSTO,$OMP_SEQFRAC_MEAN" >> "$CSV_EXT"
  done
done

echo
echo "CSV legado salvo em: $CSV_MAIN"
echo "CSV estendido salvo em: $CSV_EXT"
echo
echo "Legenda (na tabela do terminal):"
echo "  T_seq = tempo sequencial clássico; T_serEQ = tempo da versão serial-equivalente;"
echo "  T_omp = tempo com p threads; S_justo = T_serEQ/T_omp; Eficiência = S_justo/p;"
echo "  seq_frac = fração sequencial média reportada pela versão OMP."
