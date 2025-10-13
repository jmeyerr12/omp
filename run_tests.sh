#!/bin/bash
# Runner completo:
#  Parte A (base): SEQ + OMP  -> results_sd.csv
#  Parte B (extra): OMP-only  -> results_omp_only.csv
#
# Configuração por variáveis de ambiente:
#   REPS=20 THREADS="1 2 4 8 16"
#   INPUTS="inputs/*.txt inputs/*.in"            # Parte A
#   OMP_ONLY_INPUTS="inputs/input_140.txt ..."   # Parte B (opcional)
#   SEQ=./shsup_seq_not_compat
#   OMP=./shsup_omp_not_compat
#   DO_BASE=1 DO_OMP_EXTRA=1

REPS=${REPS:-20}
THREADS="${THREADS:-1 2 4 8 16}"

INPUTS=${INPUTS:-"inputs/*.txt inputs/*.in"}  # Parte A (seq+omp)
OMP_ONLY_INPUTS=${OMP_ONLY_INPUTS:-"inputs/input_140.txt inputs/input_150.txt inputs/input_160.txt inputs/input_170.txt inputs/input_180.txt inputs/input_190.txt inputs/input_200.txt"}  # Parte B (omp-only); pode ficar vazio

DO_BASE=${DO_BASE:-1}
DO_OMP_EXTRA=${DO_OMP_EXTRA:-1}

SEQ="${SEQ:-./shsup_seq_not_compat}"
OMP="${OMP:-./shsup_omp_not_compat}"

CSV_BASE="${CSV_OUT:-results_sd.csv}"
CSV_OMP_ONLY="${CSV_OMP_ONLY:-results_omp_only.csv}"

# --- util: média e desvio padrão (amostral) a partir de uma coluna de números
mean_sd() {
  awk '{
    x[NR]=$1; s+=$1
  } END {
    if (NR==0) { printf("NaN,NaN\n"); exit }
    m=s/NR; ss=0
    for(i=1;i<=NR;i++){ d=x[i]-m; ss+=d*d }
    sd=(NR>1)?sqrt(ss/(NR-1)):0
    printf("%.6f,%.6f\n", m, sd)
  }'
}

# --- util: extrai o ÚLTIMO token numérico da saída (cobre stdout/stderr mistos)
extract_time() {
  awk '{
    for(i=NF;i>=1;i--) if($i ~ /^-?[0-9]+(\.[0-9]+)?$/){print $i; exit}
  }'
}

# --- util: expande padrões e ordena "naturalmente" (70 < 100)
expand_and_sort() {
  local patterns=("$@")
  local tmp=()
  shopt -s nullglob
  for pat in "${patterns[@]}"; do
    for f in $pat; do tmp+=("$f"); done
  done
  printf '%s\n' "${tmp[@]}" | sort -V
}

# -------------------- Parte A: SEQ + OMP --------------------
if [ "$DO_BASE" -eq 1 ]; then
  echo "input,threads,seq_mean_s,seq_sd_s,omp_mean_s,omp_sd_s,speedup,efficiency" > "$CSV_BASE"

  mapfile -t files_base < <(expand_and_sort $INPUTS)
  for in_file in "${files_base[@]}"; do
    [ -f "$in_file" ] || continue
    base=$(basename "$in_file")
    echo "Base: $base"

    # Sequencial: agora lê o tempo IMPRESSO pelo binário (último número)
    seq_list=""
    for ((i=1;i<=REPS;i++)); do
      val=$("$SEQ" < "$in_file" 2>&1 | extract_time)
      seq_list+="$val"$'\n'
    done
    IFS=, read -r seq_mean seq_sd <<< "$(printf "%s" "$seq_list" | mean_sd)"
    echo "  seq  mean=$seq_mean  sd=$seq_sd"

    # Paralelo: lê o tempo IMPRESSO pelo binário OMP (último número)
    for t in $THREADS; do
      omp_list=""
      for ((i=1;i<=REPS;i++)); do
        val=$(OMP_NUM_THREADS="$t" "$OMP" < "$in_file" 2>&1 | extract_time)
        omp_list+="$val"$'\n'
      done
      IFS=, read -r omp_mean omp_sd <<< "$(printf "%s" "$omp_list" | mean_sd)"

      speedup=$(awk -v a="$seq_mean" -v b="$omp_mean" 'BEGIN{if(b>0) printf("%.6f", a/b); else print "Inf"}')
      efficiency=$(awk -v s="$speedup" -v th="$t" 'BEGIN{if(th>0) printf("%.6f", s/th); else print "NaN"}')

      echo "$base,$t,$seq_mean,$seq_sd,$omp_mean,$omp_sd,$speedup,$efficiency" >> "$CSV_BASE"
      echo "  threads=$t  omp  mean=$omp_mean  sd=$omp_sd  | speedup=$speedup  eff=$efficiency"
    done
  done
  echo "Resultados (base) em $CSV_BASE"
fi

# -------------------- Parte B: OMP-ONLY (inputs extras) --------------------
if [ "$DO_OMP_EXTRA" -eq 1 ] && [ -n "$OMP_ONLY_INPUTS" ]; then
  echo "input,threads,N,omp_mean_s,omp_sd_s,throughput_strings_per_s" > "$CSV_OMP_ONLY"

  mapfile -t files_omp_only < <(expand_and_sort $OMP_ONLY_INPUTS)
  for in_file in "${files_omp_only[@]}"; do
    [ -f "$in_file" ] || continue
    base=$(basename "$in_file")
    N=$(head -n 1 "$in_file" 2>/dev/null)
    echo "OMP-only: $base (N=$N)"

    for t in $THREADS; do
      omp_list=""
      for ((i=1;i<=REPS;i++)); do
        val=$(OMP_NUM_THREADS="$t" "$OMP" < "$in_file" 2>&1 | extract_time)
        omp_list+="$val"$'\n'
      done
      IFS=, read -r omp_mean omp_sd <<< "$(printf "%s" "$omp_list" | mean_sd)"
      thrpt=$(awk -v n="$N" -v m="$omp_mean" 'BEGIN{ if(m>0) printf("%.3f", n/m); else print "NaN"}')

      echo "$base,$t,$N,$omp_mean,$omp_sd,$thrpt" >> "$CSV_OMP_ONLY"
      echo "  threads=$t  omp  mean=$omp_mean  sd=$omp_sd  thrpt=$thrpt"
    done
  done
  echo "Resultados (omp-only) em $CSV_OMP_ONLY"
fi
