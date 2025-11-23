#!/bin/bash
#SBATCH -p cpu
#SBATCH --job-name=ssp_seq
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --time=02:00:00
#SBATCH --output=logs/seq_%j.out
#SBATCH --error=logs/seq_%j.err

set -euo pipefail

BIN=./ssp_seq          # confere se este é o nome certo do binário
CSV=results/seq_times.csv
REPS=${REPS:-20}

mkdir -p logs results

# cabeçalho (com frações agora)
if [ ! -s "$CSV" ]; then
  echo "input,tempo,sd,seq_frac,par_frac" > "$CSV"
fi

# garante que existe pelo menos um input
shopt -s nullglob
INPUTS=(inputs/*.txt)
if [ ${#INPUTS[@]} -eq 0 ]; then
  echo "Nenhum arquivo em inputs/*.txt"
  exit 1
fi

# percorre inputs em ordem
for INPUT in "${INPUTS[@]}"; do
  INBASE=$(basename "$INPUT")

  declare -a TIMES=()
  declare -a SEQFRACS=()

  for r in $(seq 1 "$REPS"); do
    ERRFILE=$(mktemp)

    # stdout: superstring + tempo total
    # stderr: "elapsed par_time seq_frac"
    OUT=$("$BIN" < "$INPUT" 2> "$ERRFILE")
    T=$(echo "$OUT" | tail -n1)

    read TOTAL PAR SEQF < "$ERRFILE"
    rm -f "$ERRFILE"

    TIMES+=("$T")
    SEQFRACS+=("$SEQF")

    echo "SEQ $INBASE rep=$r tempo=${T}s seq_frac=${SEQF}"
  done

  # média e desvio-padrão do tempo
  read -r MEAN SD < <(printf '%s\n' "${TIMES[@]}" \
    | awk '{n++; s+=$1; ss+=$1*$1} END{m=s/n; sd=(n>1?sqrt((ss-n*m*m)/(n-1)):0); printf "%.6f %.6f", m, sd}')

  # média da fração sequencial
  read -r MEAN_SEQFRAC < <(printf '%s\n' "${SEQFRACS[@]}" \
    | awk '{n++; s+=$1} END{m=(n>0?s/n:0); printf "%.6f", m}')

  PAR_FRAC=$(awk "BEGIN{printf \"%.6f\", 1.0 - $MEAN_SEQFRAC}")

  echo "$INBASE,$MEAN,$SD,$MEAN_SEQFRAC,$PAR_FRAC" >> "$CSV"
  echo "SEQ OK: $INBASE tempo(média)=${MEAN}s sd=${SD} seq_frac=${MEAN_SEQFRAC} par_frac=${PAR_FRAC}"
done
