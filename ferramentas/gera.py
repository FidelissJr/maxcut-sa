#!/usr/bin/env python3
"""Gerador de grafos aleatorios no formato do enunciado.

Uso: gera.py <n> <m> <semente> > grafo.txt

Sorteia m arestas distintas (sem lacos e sem repeticao) entre n vertices,
cada uma com peso +1 ou -1 com a mesma probabilidade. Nao e o gerador dos
professores (que e secreto), mas segue a mesma descricao: com m = 5n o grau
medio e 10, como nas instancias da competicao.
"""
import random
import sys


def main():
    if len(sys.argv) != 4:
        print("uso: gera.py <n> <m> <semente>", file=sys.stderr)
        sys.exit(2)
    n, m, semente = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    if m > n * (n - 1) // 2:
        print("m grande demais para n vertices", file=sys.stderr)
        sys.exit(2)

    sorteio = random.Random(semente)
    usadas = set()
    linhas = [f"{n} {m}"]
    while len(usadas) < m:
        u = sorteio.randint(1, n)
        v = sorteio.randint(1, n)
        if u == v:
            continue
        par = (min(u, v), max(u, v))
        if par in usadas:
            continue
        usadas.add(par)
        peso = 1 if sorteio.random() < 0.5 else -1
        linhas.append(f"{par[0]} {par[1]} {peso}")
    sys.stdout.write("\n".join(linhas) + "\n")


if __name__ == "__main__":
    main()
