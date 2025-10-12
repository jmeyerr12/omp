#!/usr/bin/env bash

# Runner completo:
#  Parte A (base): SEQ + OMP  -> results_sd.csv
#  Parte B (extra): OMP-only  -> results_omp_only.csv
#
# Configuração por variáveis de ambiente:
#   REPS=20 THREADS="1 2 4 8 16"
#   INPUTS="inputs/*.txt inputs/*.in"            # Parte A
#   OMP_ONLY_INPUTS="inputs/input_140.txt inputs/input_150.txt ..."   # Parte B (opcional)
#   SEQ=./shsup_seq_not_compat
#   OMP=./shsup_omp_not_compat
#   DO_BASE=1 DO_OMP_EXTRA=1

REPS=${REPS:-20}
THREADS="${THREADS:-1 2 4 8 16}"

INPUTS=${INPUTS:-"inputs/*.txt inputs/*.in"}     # Parte A (seq+omp)
MP_ONLY_INPUTS=${OMP_ONLY_INPUTS:-"inputs/input_140.txt inputs/input_150.txt inputs/input_160.txt inputs/input_170.txt 
                                    inputs/input_180.txt inputs/input_190.txt inputs/input_200.txt inputs/input_210.txt inputs/input_220.txt 
                                    inputs/input_230.txt inputs/input_240.txt inputs/input_250.txt"}           # Parte B (omp-only); pode ficar vazio

DO_BASE=${DO_BASE:-1}
DO_OMP_EXTRA=${DO_OMP_EXTRA:-1}

SEQ="${SEQ:-./shsup_seq_not_compat}"
OMP="${OMP:-./shsup_omp_not_compat}"

CSV_BASE="${CSV_OUT:-results_sd.csv}"
CSV_OMP_ONLY="${CSV_OMP_ONLY:-results_omp_only.csv}"

# ---------------- utils ----------------
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

# extrai 3 números (total, parallel, f_seq) da saída (stdout+stderr)
extract_omp_triplet() {
  awk '{
    for(i=1;i<=NF;i++){
      if($i ~ /^-?[0-9]+(\.[0-9]+)?$/){ nums[++c]=$i }
    }
  } END {
    if(c>=3) { printf("%s,%s,%s\n", nums[1], nums[2], nums[3]) }
    else if(c==2){ printf("%s,%s,\n", nums[1], nums[2]) }
    else if(c==1){ printf("%s,,\n", nums[1]) }
    else { print ",," }
  }'
}

# ordem “natural” (70 < 100)
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
  echo "input,threads,seq_mean_s,seq_sd_s,omp_total_mean_s,omp_total_sd_s,omp_par_mean_s,omp_par_sd_s,fseq_mean,speedup,efficiency" > "$CSV_BASE"

  mapfile -t files_base < <(expand_and_sort $INPUTS)
  for in_file in "${files_base[@]}"; do
    [ -f "$in_file" ] || continue
    base=$(basename "$in_file")
    echo "Base: $base"

    # Sequencial: mede com /usr/bin/time; descarta a superstring
    seq_list=""
    for ((i=1;i<=REPS;i++)); do
      t=$(/usr/bin/time -f "%e" bash -c "$SEQ < \"$in_file\" > /dev/null" 2>&1)
      seq_list+="$t"$'\n'
    done
    IFS=, read -r seq_mean seq_sd <<< "$(printf "%s" "$seq_list" | mean_sd)"
    echo "  seq  mean=$seq_mean  sd=$seq_sd"

    # Paralelo: lê total, parallel, f_seq (impressos pelo OMP em stderr)
    for t in $THREADS; do
      omp_total_list=""
      omp_par_list=""
      fseq_list=""
      for ((i=1;i<=REPS;i++)); do
        triplet=$(OMP_NUM_THREADS="$t" "$OMP" < "$in_file" 2>&1 | extract_omp_triplet)
        # triplet: total,parallel,fseq
        IFS=, read -r tt pp ff <<< "$triplet"
        omp_total_list+="$tt"$'\n'
        [ -n "$pp" ] && omp_par_list+="$pp"$'\n'
        [ -n "$ff" ] && fseq_list+="$ff"$'\n'
      done

      IFS=, read -r omp_total_mean omp_total_sd <<< "$(printf "%s" "$omp_total_list" | mean_sd)"
      # parallel e fseq podem estar vazios se o binário não imprimir — tratamos isso
      if [ -n "$omp_par_list" ]; then
        IFS=, read -r omp_par_mean omp_par_sd <<< "$(printf "%s" "$omp_par_list" | mean_sd)"
      else
        omp_par_mean="NaN"; omp_par_sd="NaN"
      fi
      if [ -n "$fseq_list" ]; then
        # média simples do f_seq (sem sd porque é fração, mas poderia calcular)
        fseq_mean=$(awk '{s+=$1; n+=1} END{if(n>0) printf("%.6f", s/n); else print "NaN"}' <<< "$fseq_list")
      else
        fseq_mean="NaN"
      fi

      speedup=$(awk -v a="$seq_mean" -v b="$omp_total_mean" 'BEGIN{if(b>0) printf("%.6f", a/b); else print "Inf"}')
      efficiency=$(awk -v s="$speedup" -v th="$t" 'BEGIN{if(th>0) printf("%.6f", s/th); else print "NaN"}')

      echo "$base,$t,$seq_mean,$seq_sd,$omp_total_mean,$omp_total_sd,$omp_par_mean,$omp_par_sd,$fseq_mean,$speedup,$efficiency" >> "$CSV_BASE"
      echo "  T=$t  omp  total=$omp_total_mean sd=$omp_total_sd | par=$omp_par_mean sd=$omp_par_sd | fseq=$fseq_mean | speedup=$speedup eff=$efficiency"
    done
  done
  echo "Resultados (base) em $CSV_BASE"
fi

# -------------------- Parte B: OMP-ONLY (inputs extras) --------------------
if [ "$DO_OMP_EXTRA" -eq 1 ] && [ -n "$OMP_ONLY_INPUTS" ]; then
  echo "input,threads,N,omp_total_mean_s,omp_total_sd_s,omp_par_mean_s,omp_par_sd_s,fseq_mean,throughput_strings_per_s" > "$CSV_OMP_ONLY"

  mapfile -t files_omp_only < <(expand_and_sort $OMP_ONLY_INPUTS)
  for in_file in "${files_omp_only[@]}"; do
    [ -f "$in_file" ] || continue
    base=$(basename "$in_file")
    N=$(head -n 1 "$in_file")
    echo "OMP-only: $base (N=$N)"

    for t in $THREADS; do
      omp_total_list=""
      omp_par_list=""
      fseq_list=""

      for ((i=1;i<=REPS;i++)); do
        triplet=$(OMP_NUM_THREADS="$t" "$OMP" < "$in_file" 2>&1 | extract_omp_triplet)
        IFS=, read -r tt pp ff <<< "$triplet"
        omp_total_list+="$tt"$'\n'
        [ -n "$pp" ] && omp_par_list+="$pp"$'\n'
        [ -n "$ff" ] && fseq_list+="$ff"$'\n'
      done

      IFS=, read -r omp_total_mean omp_total_sd <<< "$(printf "%s" "$omp_total_list" | mean_sd)"
      if [ -n "$omp_par_list" ]; then
        IFS=, read -r omp_par_mean omp_par_sd <<< "$(printf "%s" "$omp_par_list" | mean_sd)"
      else
        omp_par_mean="NaN"; omp_par_sd="NaN"
      fi
      if [ -n "$fseq_list" ]; then
        fseq_mean=$(awk '{s+=$1; n+=1} END{if(n>0) printf("%.6f", s/n); else print "NaN"}' <<< "$fseq_list")
      else
        fseq_mean="NaN"
      fi

      thrpt=$(awk -v n="$N" -v m="$omp_total_mean" 'BEGIN{ if(m>0) printf("%.3f", n/m); else print "NaN"}')

      echo "$base,$t,$N,$omp_total_mean,$omp_total_sd,$omp_par_mean,$omp_par_sd,$fseq_mean,$thrpt" >> "$CSV_OMP_ONLY"
      echo "  T=$t  omp  total=$omp_total_mean sd=$omp_total_sd | par=$omp_par_mean sd=$omp_par_sd | fseq=$fseq_mean | thrpt=$thrpt"
    done
  done
  echo "Resultados (omp-only) em $CSV_OMP_ONLY"
fi
