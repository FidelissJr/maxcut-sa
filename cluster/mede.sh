#!/bin/bash
#SBATCH --job-name=maxcut
#SBATCH --nodes=1
#SBATCH --exclusive
#
# Medicoes do maxcut no cluster. Leva cerca de 12 minutos em um no.
#
# Uso, a partir da raiz do projeto, na maquina de entrada (baia0):
#
#     bash cluster/mede.sh
#
# O roteiro anota os dados da maquina de entrada, envia a si mesmo para a fila
# do SLURM (um no inteiro, com exclusividade) e avisa o nome do arquivo de
# resultado. E esse arquivo, em cluster/resultado-*.txt, que interessa.
#
# Ajustes opcionais, por variavel de ambiente:
#   DURACAO=10        segundos de cada medicao curta
#   REPETICOES=2      vezes que cada configuracao e medida
#   TEMPO_COMPLETO=   tempo da execucao completa (vazio = padrao do programa)
#   MEDE_DIRETO=1     executa aqui mesmo, sem passar pelo SLURM

DURACAO=${DURACAO:-10}
REPETICOES=${REPETICOES:-2}

# Dentro do SLURM o roteiro e copiado para outra pasta, entao $0 nao serve
# para achar o projeto; o SLURM informa de onde o trabalho foi enviado.
if [ -n "$SLURM_SUBMIT_DIR" ]; then
    cd "$SLURM_SUBMIT_DIR" || exit 1
else
    cd "$(dirname "$0")/.." || exit 1
fi
if [ ! -f src/maxcut.c ]; then
    echo "mede.sh: execute a partir da raiz do projeto (onde fica o Makefile)" >&2
    exit 1
fi

# Ferramentas e instancias oficiais do professor (pasta tf-maxcut).
TF=tf-maxcut
VER="$TF/verificador"
GRANDE="$TF/entradas/n100000_s1.txt"   # instancia Grande; melhor conhecido 116548
MEDIA="$TF/entradas/n20000_s1.txt"     # instancia Media;  melhor conhecido 23668

# ---------------------------------------------------------------------------
# Passo 0: na maquina de entrada, anota os dados dela e envia para a fila.
# ---------------------------------------------------------------------------
if [ -z "$SLURM_JOB_ID" ] && [ -z "$MEDE_DIRETO" ] && command -v sbatch > /dev/null; then
    {
        echo "maquina de entrada: $(hostname)"
        lscpu 2> /dev/null | grep -E 'Model name|Nome do modelo|Flags|Opções' | cut -c1-2000
        gcc --version 2> /dev/null | head -1
        mpirun --version 2> /dev/null | head -1
    } > cluster/entrada.txt 2>&1
    export DURACAO REPETICOES TEMPO_COMPLETO
    sbatch --nodes=1 --exclusive --export=ALL cluster/mede.sh || exit 1
    echo
    echo "Enviado para a fila. Acompanhe com:  squeue -u $USER"
    echo "Quando o trabalho sair da fila, o resultado estara em cluster/resultado-*.txt"
    echo "(a ultima linha do arquivo deve ser FIM DA MEDICAO)."
    exit 0
fi

RESULTADO=cluster/resultado-$(hostname)-$(date +%Y%m%d-%H%M%S).txt
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# agora: segundos com fracao, para medir tempo de relogio.
if date +%N | grep -q '^[0-9][0-9]*$'; then
    agora() { date +%s.%N; }
else
    agora() { python3 -c 'import time; print(time.time())'; }   # macOS
fi
desde() { awk -v a="$1" -v b="$(agora)" 'BEGIN { printf "%.2f", b - a }'; }

titulo() { echo; echo "=================== $1 ==================="; }

# ordem_cpus: lista os processadores na ordem em que vale a pena usa-los.
#   1o: um fio de cada nucleo rapido (os que tem dois fios / maior frequencia);
#   2o: os nucleos economicos;
#   3o: o segundo fio dos nucleos rapidos (hyper-threading).
ordem_cpus() {
    command -v lscpu > /dev/null || return
    local tabela
    tabela=$(lscpu -p=CPU,CORE,MAXMHZ 2> /dev/null | grep -v '^#')
    [ -z "$tabela" ] && tabela=$(lscpu -p=CPU,CORE 2> /dev/null | grep -v '^#')
    echo "$tabela" | awk -F, '
        NF >= 2 { cpu[NR] = $1; nucleo[NR] = $2; mhz[NR] = ($3 == "" ? 0 : $3); fios[$2]++; n = NR }
        END {
            for (i = 1; i <= n; i++) {
                segundo = (nucleo[i] in visto) ? 1 : 0
                visto[nucleo[i]] = 1
                # colunas: segundo fio? | menos fios primeiro? | frequencia | cpu
                printf "%d %d %012.3f %d\n", segundo, 9 - fios[nucleo[i]], 1000000 - mhz[i], cpu[i]
            }
        }' | sort -k1,1n -k2,2n -k3,3n -k4,4n | awk '{ printf "%s ", $4 }'
}

# roda <instancia> <ranks> <threads> <tempo> <saida>
# Executa uma vez e deixa em variaveis: codigo, relogio, varreduras, corte,
# interno (tempo que o proprio programa informa) e verificado.
roda() {
    local inicio
    inicio=$(agora)
    if [ -n "$4" ]; then
        MAXCUT_TEMPO=$4 mpirun $FLAGS_MPI -np "$2" bash cluster/fixa.sh "$3" ./maxcut "$1" "$5" 2> "$TMP/log"
    else
        mpirun $FLAGS_MPI -np "$2" bash cluster/fixa.sh "$3" ./maxcut "$1" "$5" 2> "$TMP/log"
    fi
    codigo=$?
    relogio=$(desde "$inicio")
    varreduras=$(sed -n 's/.*varreduras=\([0-9]*\).*/\1/p' "$TMP/log" | tail -1)
    corte=$(sed -n 's/.*corte=\(-*[0-9]*\).*/\1/p' "$TMP/log" | tail -1)
    interno=$(sed -n 's/.*tempo=\([0-9.]*\)s.*/\1/p' "$TMP/log" | tail -1)
    verificado=$("$VER" "$1" "$5" 2> /dev/null)
    if [ $codigo -ne 0 ] || [ -z "$corte" ]; then
        echo "    (falhou, codigo $codigo) $(tr '\n' ' ' < "$TMP/log" | cut -c1-600)"
    fi
}

cabecalho() {
    printf '%-8s %5s %7s %3s %11s %9s %10s %8s %8s\n' \
        instancia ranks threads rep varreduras corte verificado interno relogio
}
linha() {   # linha <nome> <ranks> <threads> <rep>
    printf '%-8s %5s %7s %3s %11s %9s %10s %8s %8s\n' \
        "$1" "$2" "$3" "$4" "${varreduras:--}" "${corte:--}" "${verificado:--}" "${interno:--}" "$relogio"
}

principal() {
    titulo "AMBIENTE"
    echo "data: $(date)"
    echo "maquina: $(hostname)   sistema: $(uname -sr)"
    echo "pasta: $(pwd)"
    echo "carga: $(uptime)"
    [ -f cluster/entrada.txt ] && { echo "--- maquina de entrada"; cat cluster/entrada.txt; }
    echo "--- processador"
    lscpu 2> /dev/null | grep -E 'Model name|Nome do modelo|^CPU\(s\)|Thread|Core|Socket|MHz'
    lscpu -e 2> /dev/null
    echo "processadores liberados para este trabalho: $(nproc 2> /dev/null || sysctl -n hw.ncpu)"
    grep Cpus_allowed_list /proc/self/status 2> /dev/null
    free -g 2> /dev/null | head -2
    echo "--- compiladores"
    gcc --version 2>&1 | head -1
    gcc -march=native -Q --help=target 2> /dev/null | grep -E '^ *-march='
    mpicc --version 2>&1 | head -1
    mpicc -show 2>&1 | head -1
    mpirun --version 2>&1 | head -2
    python3 --version 2>&1
    echo "--- variaveis do SLURM"
    env | grep '^SLURM_' | sort
    # Exemplos do proprio laboratorio de como lancar MPI em lote: servem para
    # copiar a forma oficial no comando final de execucao.
    for arquivo in /mnt/prj/tools/README.slurm-batch /mnt/prj/tools/slurm-tests/runbash.sh; do
        [ -r "$arquivo" ] && { echo "--- $arquivo"; cat "$arquivo"; }
    done

    NCPUS=$(nproc 2> /dev/null || sysctl -n hw.ncpu)

    # Opcoes do mpirun. No Open MPI: nao prender os ranks (quem prende e o
    # fixa.sh) e aceitar mais ranks do que as vagas que o SLURM reservou.
    FLAGS_MPI=""
    if mpirun --version 2>&1 | grep -qi 'open.mpi'; then
        FLAGS_MPI="--bind-to none --oversubscribe"
    fi

    titulo "MPIRUN SEM OPCOES"
    # Mostra se um "mpirun -np N" simples, como o avaliador talvez use, funciona.
    for np in 2 8 16; do
        if mpirun -np $np true > "$TMP/puro" 2>&1; then
            echo "mpirun -np $np: funciona"
        else
            echo "mpirun -np $np: FALHA -> $(tr '\n' ' ' < "$TMP/puro" | cut -c1-300)"
        fi
    done
    echo "opcoes usadas daqui em diante: mpirun $FLAGS_MPI"

    titulo "COMPILACAO"
    make clean
    if [ "$(uname)" = Linux ]; then
        make ARQ=x86-64-v3 2>&1 && cp maxcut "$TMP/maxcut-v3"
        make clean
    fi
    make 2>&1 || { echo "COMPILACAO FALHOU"; return 1; }
    make referencia 2>&1
    make -C "$TF" 2>&1        # gerador e verificador oficiais

    titulo "TESTES"
    export OMP_WAIT_POLICY=active
    MPIRUN_FLAGS="$FLAGS_MPI" bash testes/testa.sh 2>&1

    titulo "FIXACAO DOS PROCESSADORES"
    MAXCUT_ORDEM_CPUS=$(ordem_cpus)
    if [ -n "$MAXCUT_ORDEM_CPUS" ] && taskset -c "$(echo $MAXCUT_ORDEM_CPUS | tr ' ' ',')" true 2> /dev/null; then
        echo "ordem de uso: $MAXCUT_ORDEM_CPUS"
        echo "exemplo, 2 ranks de 4 threads:"
        mpirun $FLAGS_MPI -np 2 bash cluster/fixa.sh 4 grep Cpus_allowed_list /proc/self/status 2>&1
    else
        echo "sem lscpu/taskset ou processadores restritos: as medicoes rodam SEM fixacao"
        MAXCUT_ORDEM_CPUS=""
    fi
    export MAXCUT_ORDEM_CPUS

    titulo "REFERENCIA SEQUENCIAL (Algoritmo 1, instancia Grande)"
    echo "melhor corte conhecido da n100000_s1: 116548"
    for v in 100 1000; do
        inicio=$(agora)
        ./referencia "$GRANDE" $v 1 2>&1 | tail -1
        echo "    $v varreduras em $(desde "$inicio") s"
    done

    if [ -f "$TMP/maxcut-v3" ]; then
        titulo "NATIVE x X86-64-V3 (instancia grande, 5 s)"
        cabecalho
        cp maxcut "$TMP/maxcut-native"
        for versao in native v3; do
            cp "$TMP/maxcut-$versao" maxcut
            for t in 1 8; do
                [ "$t" -le "$NCPUS" ] || continue
                roda "$GRANDE" 1 $t 5 "$TMP/sol"
                linha "$versao" 1 $t 1
            done
        done
        cp "$TMP/maxcut-native" maxcut
    fi

    titulo "RANKS x THREADS ($DURACAO s por execucao)"
    # 1 rank com 1 a 24 threads mostra o ganho das threads; as outras linhas
    # repartem 8 nucleos (os rapidos) e 16 nucleos (todos) entre ranks e threads.
    cabecalho
    for par in "grande $GRANDE" "media $MEDIA"; do
        set -- $par; nome=$1; inst=$2
        for config in "1 1" "1 2" "1 4" "1 8" "1 16" "1 24" \
                      "2 4" "4 2" "8 1" "2 8" "4 4" "16 1"; do
            set -- $config
            [ $(($1 * $2)) -le "$NCPUS" ] || continue
            for rep in $(seq 1 "$REPETICOES"); do
                roda "$inst" "$1" "$2" "$DURACAO" "$TMP/sol"
                linha "$nome" "$1" "$2" "$rep"
            done
        done
    done

    titulo "EXECUCAO COMPLETA (tempo padrao do programa, saida na pasta do projeto)"
    # O que importa aqui e a coluna "relogio": tem de ficar abaixo de 90 s.
    # Melhor corte conhecido: Grande 116548, Media 23668.
    cabecalho
    ranks=4; threads=4
    [ $((ranks * threads)) -le "$NCPUS" ] || { ranks=2; threads=2; }
    for par in "grande $GRANDE" "media $MEDIA"; do
        set -- $par; nome=$1; inst=$2
        roda "$inst" $ranks $threads "$TEMPO_COMPLETO" cluster/saida-$nome.txt
        linha "$nome" $ranks $threads 1
        ls -l cluster/saida-$nome.txt 2>&1
        rm -f cluster/saida-$nome.txt
    done

    echo
    echo "FIM DA MEDICAO"
}

principal 2>&1 | tee "$RESULTADO"
echo "resultado gravado em $RESULTADO" >&2
