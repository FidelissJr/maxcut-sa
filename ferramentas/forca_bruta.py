#!/usr/bin/env python3
"""Corte maximo exato por forca bruta (so para grafos pequenos, n <= 22).

Uso: forca_bruta.py <arquivo_do_grafo>

Testa todas as particoes e imprime o maior corte. Serve para conferir se o
programa principal chega ao otimo em grafos onde o otimo e conhecido.

Dois detalhes para nao demorar:
- O ultimo vertice fica fixo no grupo A. Trocar todos os vertices de lado
  da o mesmo corte, entao isso corta as particoes pela metade sem perder nada.
- As particoes sao visitadas em "codigo de Gray": de uma para a seguinte
  muda um unico vertice, entao o corte e atualizado com o delta desse
  vertice em vez de ser recalculado do zero.
"""
import sys


def main():
    with open(sys.argv[1]) as arquivo:
        numeros = arquivo.read().split()
    n, m = int(numeros[0]), int(numeros[1])
    if n > 22:
        print("grafo grande demais para forca bruta", file=sys.stderr)
        sys.exit(2)

    vizinhos = [[] for _ in range(n)]
    for k in range(m):
        u = int(numeros[2 + 3 * k]) - 1
        v = int(numeros[3 + 3 * k]) - 1
        w = int(numeros[4 + 3 * k])
        if u != v:
            vizinhos[u].append((v, w))
            vizinhos[v].append((u, w))

    lado = [0] * n          # comeca com todos no grupo A: corte 0
    corte = 0
    melhor = 0
    for passo in range(1, 2 ** (n - 1)):
        # No codigo de Gray, o vertice que muda no passo k e a posicao do
        # bit 1 mais baixo de k.
        i = (passo & -passo).bit_length() - 1
        for j, w in vizinhos[i]:
            if lado[j] == lado[i]:
                corte += w      # a aresta passa a cruzar
            else:
                corte -= w      # a aresta deixa de cruzar
        lado[i] = 1 - lado[i]
        if corte > melhor:
            melhor = corte
    print(melhor)


if __name__ == "__main__":
    main()
