#!/usr/bin/env python3
"""Verificador independente do corte.

Uso: verifica.py <arquivo_do_grafo> <arquivo_da_solucao>

Faz o papel do "verificador externo" do enunciado: le o grafo e a solucao,
confere o formato e imprime o valor do corte. Foi escrito em outra linguagem
e sem reaproveitar nada do programa principal, justamente para nao repetir
um eventual erro dele.
"""
import sys


def le_grafo(caminho):
    """Devolve (n, lista de arestas (u, v, w)) com vertices numerados de 1 a n."""
    with open(caminho) as arquivo:
        numeros = arquivo.read().split()
    n, m = int(numeros[0]), int(numeros[1])
    arestas = []
    for k in range(m):
        u = int(numeros[2 + 3 * k])
        v = int(numeros[3 + 3 * k])
        w = int(numeros[4 + 3 * k])
        arestas.append((u, v, w))
    return n, arestas


def le_solucao(caminho, n):
    """Devolve a string de n caracteres 0/1, ou None se o formato for invalido."""
    try:
        with open(caminho) as arquivo:
            conteudo = arquivo.read()
    except OSError:
        return None     # o programa nem gravou a saida
    # Uma unica linha: aceitamos com ou sem a quebra de linha no final.
    if conteudo.endswith("\n"):
        conteudo = conteudo[:-1]
    if len(conteudo) != n:
        return None
    if any(c not in "01" for c in conteudo):
        return None
    return conteudo


def valor_do_corte(arestas, x):
    """Soma os pesos das arestas cujas pontas estao em lados diferentes."""
    total = 0
    for u, v, w in arestas:
        if x[u - 1] != x[v - 1]:
            total += w
    return total


def main():
    if len(sys.argv) != 3:
        print("uso: verifica.py <grafo> <solucao>", file=sys.stderr)
        sys.exit(2)
    n, arestas = le_grafo(sys.argv[1])
    x = le_solucao(sys.argv[2], n)
    if x is None:
        print("INVALIDA")
        sys.exit(1)
    print(valor_do_corte(arestas, x))


if __name__ == "__main__":
    main()
