#ifndef GRAFO_H
#define GRAFO_H

/*
 * Grafo pronto para a varredura paralela.
 *
 * Os vizinhos ficam no formato CSR (uma lista unica, com um indice dizendo
 * onde comeca o trecho de cada vertice): os vizinhos do vertice i estao em
 * viz[ini[i]] ... viz[ini[i+1] - 1], e peso[k] e o peso da aresta que leva
 * a viz[k]. Cada aresta aparece duas vezes, uma em cada ponta.
 *
 * Os vertices sao RENUMERADOS por cor: primeiro todos os da cor 0, depois
 * todos os da cor 1, e assim por diante. Assim, "processar a cor c" e
 * percorrer o trecho continuo ini_cor[c] ... ini_cor[c+1] - 1, o que e
 * simples de repartir entre threads e bom para o cache. O vetor original[]
 * guarda o numero que cada vertice tinha no arquivo, para a saida.
 */
typedef struct {
    int n;              /* numero de vertices */
    int m;              /* numero de arestas */
    int *ini;           /* n + 1 posicoes: onde comecam os vizinhos de cada vertice */
    int *viz;           /* 2m posicoes: os vizinhos */
    signed char *peso;  /* 2m posicoes: o peso (+1 ou -1) de cada aresta */
    int grau_max;       /* maior numero de vizinhos de um vertice */
    int num_cores;      /* quantas cores a coloracao usou */
    int *ini_cor;       /* num_cores + 1 posicoes: onde comeca cada cor */
    int *original;      /* n posicoes: numero do vertice no arquivo (a partir de 0) */
} Grafo;

/*
 * Le o arquivo do enunciado. Devolve 0 se deu certo. Os vetores u, v e w
 * sao alocados aqui, com uma posicao por aresta e vertices a partir de 0.
 */
int le_arestas(const char *caminho, int *n, int *m,
               int **u, int **v, signed char **w);

/*
 * Monta o grafo a partir da lista de arestas: colore, renumera e cria o CSR.
 * ATENCAO: troca o conteudo de u e v pela numeracao nova.
 * O resultado depende so da lista de arestas, entao todos os ranks MPI que
 * receberem a mesma lista chegam exatamente a mesma numeracao.
 */
void monta_grafo(Grafo *g, int n, int m, int *u, int *v, const signed char *w);

void libera_grafo(Grafo *g);

#endif
