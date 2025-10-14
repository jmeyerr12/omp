#!/usr/bin/env bash
set -euo pipefail

BIN="./shsup_omp_not_compat_serial"   # <-- coloque aqui o seu executável
INFILE="inputs/input_200.txt"                   # <-- arquivo de entrada (ou remova se não precisa)
REPS=5

sum_total=0
sum_frac=0
sum_seqtime=0
count=0

for ((i=1;i<=REPS;i++)); do
  # Pega a 1a linha do stderr do binário (ignora stdout)
  line="$(( "$BIN" < "$INFILE" 1>/dev/null ) 2>&1 | tr -d '\r' | sed -n '1p')"

  # Extrai 1º número (total) e 3º número (fração sequencial)
  read total frac <<<"$(awk -v L="$line" '
    BEGIN {
      gsub(",",".",L)
      n=split(L,a,/[[:space:]]+/)
      c=0
      for(i=1;i<=n;i++)
        if (a[i] ~ /^-?[0-9]+([.][0-9]+)?$/) nums[++c]=a[i]
      if (c>=3) printf("%s %s", nums[1], nums[3])
    }')"

  # Acumula
  seqtime=$(awk -v t="$total" -v f="$frac" 'BEGIN{printf("%.10f", t*f)}')

  sum_total=$(awk -v s="$sum_total" -v x="$total" 'BEGIN{printf("%.10f", s+x)}')
  sum_frac=$(awk -v s="$sum_frac" -v x="$frac"  'BEGIN{printf("%.10f", s+x)}')
  sum_seqtime=$(awk -v s="$sum_seqtime" -v x="$seqtime" 'BEGIN{printf("%.10f", s+x)}')
  count=$((count+1))
done

# Médias
mean_total=$(awk -v s="$sum_total" -v n="$count" 'BEGIN{printf("%.6f", s/n)}')
mean_frac=$(awk  -v s="$sum_frac"  -v n="$count" 'BEGIN{printf("%.6f", s/n)}')       # fração média (0–1)
mean_seq=$(awk   -v s="$sum_seqtime" -v n="$count" 'BEGIN{printf("%.6f", s/n)}')     # tempo sequencial médio (s)

# Porcentagem (média da fração × 100)
mean_pct=$(awk -v f="$mean_frac" 'BEGIN{printf("%.2f", f*100)}')

echo "==========================================="
echo "Média do tempo TOTAL (s):       $mean_total"
echo "Média do tempo SEQUENCIAL (s):  $mean_seq"
echo "Porcentagem SEQUENCIAL média:   $mean_pct%"
echo "==========================================="