/*
 * Referencia sequencial: o Algoritmo 1 do enunciado, ao pe da letra.
 *
 * Uso: ./referencia <arquivo_do_grafo> <varreduras> [semente] [arquivo_da_solucao]
 *
 * Nao faz parte da entrega. Serve de base de comparacao: roda em um nucleo,
 * com numero fixo de varreduras, T0 = 1,5 e T1 = 0,3, rand() e exp(), como
 * a Tabela II do enunciado. Com ele da para medir, no MESMO grafo, quanto o
 * programa paralelo ganha em varreduras por segundo e em corte.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/grafo.h"

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s <grafo> <varreduras> [semente] [solucao]\n", argv[0]);
        return 2;
    }
    int S = atoi(argv[2]);
    srand(argc > 3 ? (unsigned)atoi(argv[3]) : 1u);
    const double T0 = 1.5, T1 = 0.3;

    int n, m;
    int *u, *v;
    signed char *w;
    if (le_arestas(argv[1], &n, &m, &u, &v, &w) != 0) {
        fprintf(stderr, "nao consegui ler %s\n", argv[1]);
        return 1;
    }

    /* lista de vizinhos (CSR) na ordem do arquivo */
    int *ini = calloc((size_t)n + 2, sizeof(int));
    int *viz = malloc((size_t)2 * m * sizeof(int));
    int *peso = malloc((size_t)2 * m * sizeof(int));
    for (int e = 0; e < m; e++) {
        ini[u[e] + 2]++;
        ini[v[e] + 2]++;
    }
    for (int i = 0; i < n; i++)
        ini[i + 2] += ini[i + 1];
    for (int e = 0; e < m; e++) {
        viz[ini[u[e] + 1]] = v[e];
        peso[ini[u[e] + 1]++] = w[e];
        viz[ini[v[e] + 1]] = u[e];
        peso[ini[v[e] + 1]++] = w[e];
    }
    /* agora os vizinhos de i estao em viz[ini[i]] ... viz[ini[i+1] - 1] */

    clock_t inicio = clock();

    /* linha 1: x aleatorio; calcular c(x) e delta de todo vertice */
    int *x = malloc((size_t)n * sizeof(int));
    int *delta = malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++)
        x[i] = rand() & 1;
    long c = 0;
    for (int i = 0; i < n; i++) {
        delta[i] = 0;
        for (int k = ini[i]; k < ini[i + 1]; k++)
            delta[i] += peso[k] * (2 * (x[viz[k]] == x[i]) - 1);
    }
    for (int e = 0; e < m; e++)
        if (x[u[e]] != x[v[e]])
            c += w[e];

    /* linhas 2 a 10 */
    for (int s = 0; s < S; s++) {
        double T = (S > 1) ? T0 * pow(T1 / T0, (double)s / (S - 1)) : T1;
        for (int i = 0; i < n; i++) {
            if (delta[i] >= 0 || rand() / (RAND_MAX + 1.0) < exp(delta[i] / T)) {
                c += delta[i];
                x[i] = 1 - x[i];
                delta[i] = -delta[i];
                for (int k = ini[i]; k < ini[i + 1]; k++) {
                    int j = viz[k];
                    /* a aresta i-j mudou de situacao: a parcela dela no
                     * delta de j troca de sinal */
                    delta[j] += 2 * peso[k] * (2 * (x[j] == x[i]) - 1);
                }
            }
        }
    }

    double segundos = (double)(clock() - inicio) / CLOCKS_PER_SEC;
    printf("referencia: n=%d m=%d varreduras=%d corte=%ld tempo=%.2fs (%.0f varreduras/s)\n",
           n, m, S, c, segundos, S / segundos);

    if (argc > 4) {
        FILE *arquivo = fopen(argv[4], "w");
        for (int i = 0; i < n; i++)
            fputc('0' + x[i], arquivo);
        fputc('\n', arquivo);
        fclose(arquivo);
    }
    return 0;
}
