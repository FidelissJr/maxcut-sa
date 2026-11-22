#!/bin/bash
# fixa.sh <threads> <programa> [argumentos...]
#
# Lancado pelo mpirun uma vez para cada rank. Prende o rank em um conjunto
# proprio de processadores e so entao executa o programa.
#
# Por que isso existe: o i7-13700K tem 8 nucleos rapidos (cada um com dois
# "fios", o hyper-threading) e 8 nucleos economicos. Se o sistema decidir
# sozinho, pode por duas threads no mesmo nucleo rapido e deixar outro vazio,
# ou misturar nucleos rapidos e economicos na mesma cadeia. Aqui a escolha e
# explicita.
#
# MAXCUT_ORDEM_CPUS e a lista de processadores na ordem de preferencia
# (montada pelo mede.sh). O rank r com T threads fica com as posicoes
# r*T ... r*T+T-1 dessa lista. Exemplo com a lista "0 2 4 6 8 10 12 14" e
# 2 ranks de 4 threads: o rank 0 fica com 0,2,4,6 e o rank 1 com 8,10,12,14.

threads=$1
shift

# Cada implementacao de MPI avisa o numero do rank por uma variavel diferente.
rank=${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-0}}

export OMP_NUM_THREADS=$threads

ordem=($MAXCUT_ORDEM_CPUS)
meus=("${ordem[@]:$((rank * threads)):$threads}")

if [ "${#meus[@]}" -eq "$threads" ]; then
    lista=$(IFS=,; echo "${meus[*]}")
    # Uma thread por processador da lista, sem trocar de lugar.
    export OMP_PLACES=threads
    export OMP_PROC_BIND=close
    exec taskset -c "$lista" "$@"
fi

# Sem lista (ou lista curta demais): executa sem prender.
exec "$@"
