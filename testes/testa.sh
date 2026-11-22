#!/bin/bash
# Testes de validacao do maxcut.
#
# Uso: testes/testa.sh        (a partir da raiz do projeto, depois de "make")
#
# Cada teste roda o programa de verdade e confere o resultado com o
# verificador independente (ferramentas/verifica.py). Os tempos sao curtos
# (variavel MAXCUT_TEMPO) para a bateria inteira levar menos de um minuto.

cd "$(dirname "$0")/.." || exit 1

PROG=./maxcut
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

falhas=0
passou() { echo "  ok    $1"; }
falhou() { echo "  FALHA $1"; falhas=$((falhas + 1)); }

# roda <ranks> <threads> <tempo> <grafo> <saida>
# Executa o programa e guarda o que ele escreveu na saida de erro em $TMP/log.
# MPIRUN_FLAGS (opcional) leva opcoes extras ao mpirun, por exemplo
# "--oversubscribe" quando o SLURM reservou menos vagas do que ranks.
roda() {
    MAXCUT_TEMPO=$3 MAXCUT_TROCA=0.3 MAXCUT_CHECA=1 OMP_NUM_THREADS=$2 \
        mpirun $MPIRUN_FLAGS -np "$1" $PROG "$4" "$5" 2> "$TMP/log"
}

# corte_informado: o corte que o proprio programa diz ter obtido ("corte=N").
corte_informado() { sed -n 's/.*corte=\(-*[0-9][0-9]*\).*/\1/p' "$TMP/log" | tail -1; }

echo "1. exemplo do enunciado (5 vertices, corte maximo 3)"
printf '5 6\n1 2 1\n1 3 -1\n2 3 1\n2 4 1\n3 5 1\n4 5 -1\n' > "$TMP/exemplo.txt"
roda 1 1 0.2 "$TMP/exemplo.txt" "$TMP/exemplo.sol"
corte=$(python3 ferramentas/verifica.py "$TMP/exemplo.txt" "$TMP/exemplo.sol")
[ "$corte" = "3" ] && passou "corte = 3" || falhou "esperado 3, obtido '$corte'"

echo "2. vertices sem aresta tambem aparecem na saida"
printf '5 1\n1 2 1\n' > "$TMP/isolados.txt"
roda 1 2 0.2 "$TMP/isolados.txt" "$TMP/isolados.sol"
corte=$(python3 ferramentas/verifica.py "$TMP/isolados.txt" "$TMP/isolados.sol")
[ "$corte" = "1" ] && passou "corte = 1 com saida de 5 caracteres" || falhou "esperado 1, obtido '$corte'"

echo "3. chega ao otimo (forca bruta) em 10 grafos de 16 vertices"
for semente in 1 2 3 4 5 6 7 8 9 10; do
    python3 ferramentas/gera.py 16 40 $semente > "$TMP/p$semente.txt"
    otimo=$(python3 ferramentas/forca_bruta.py "$TMP/p$semente.txt")
    roda 1 2 0.2 "$TMP/p$semente.txt" "$TMP/p$semente.sol"
    corte=$(python3 ferramentas/verifica.py "$TMP/p$semente.txt" "$TMP/p$semente.sol")
    [ "$corte" = "$otimo" ] && passou "semente $semente: corte = $otimo" \
        || falhou "semente $semente: otimo $otimo, obtido '$corte'"
done

echo "4. com 4 threads, o corte que o programa acompanha bate com o verificador"
python3 ferramentas/gera.py 1000 5000 42 > "$TMP/treino.txt"
roda 1 4 1 "$TMP/treino.txt" "$TMP/treino.sol"; codigo=$?
corte=$(python3 ferramentas/verifica.py "$TMP/treino.txt" "$TMP/treino.sol")
informado=$(corte_informado)
[ $codigo -eq 0 ] && passou "checagem interna de delta e corte sem erro" \
    || falhou "programa terminou com codigo $codigo: $(cat "$TMP/log")"
[ -n "$corte" ] && [ "$corte" = "$informado" ] && passou "informado = verificado = $corte" \
    || falhou "informado '$informado', verificado '$corte'"

echo "5. qualidade minima no grafo de treino (1000 vertices, 5000 arestas)"
# A Tabela II do enunciado da corte de cerca de 0,23 por aresta; 1100 = 0,22 * 5000.
[ -n "$corte" ] && [ "$corte" != "INVALIDA" ] && [ "$corte" -ge 1100 ] \
    && passou "corte $corte >= 1100" || falhou "corte '$corte' abaixo de 1100"

echo "6. com 2 ranks MPI e 2 threads cada, o resultado continua consistente"
roda 2 2 1 "$TMP/treino.txt" "$TMP/treino2.sol"; codigo=$?
corte=$(python3 ferramentas/verifica.py "$TMP/treino.txt" "$TMP/treino2.sol")
informado=$(corte_informado)
[ $codigo -eq 0 ] && [ -n "$corte" ] && [ "$corte" = "$informado" ] \
    && passou "informado = verificado = $corte" \
    || falhou "codigo $codigo, informado '$informado', verificado '$corte'"

echo "7. respeita o limite de tempo pedido"
inicio=$(python3 -c 'import time; print(time.time())')
roda 2 2 2 "$TMP/treino.txt" "$TMP/treino3.sol"
gasto=$(python3 -c "import time; print(time.time() - $inicio)")
python3 -c "import sys; sys.exit(0 if 1.5 <= $gasto <= 3.5 else 1)" \
    && passou "pediu 2 s, levou $gasto s" || falhou "pediu 2 s, levou $gasto s"

echo
if [ $falhas -eq 0 ]; then
    echo "TODOS OS TESTES PASSARAM"
else
    echo "$falhas TESTE(S) FALHARAM"
    exit 1
fi
