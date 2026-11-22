#include "grafo.h"

#include <stdio.h>
#include <stdlib.h>

/* malloc que encerra o programa se faltar memoria, para nao testar em todo lugar. */
static void *aloca(size_t bytes)
{
    void *p = malloc(bytes > 0 ? bytes : 1);
    if (p == NULL) {
        fprintf(stderr, "maxcut: sem memoria\n");
        exit(1);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Leitura                                                             */
/* ------------------------------------------------------------------ */

/*
 * Le o proximo numero inteiro a partir de p, pulando espacos e quebras de
 * linha. Devolve a posicao logo depois do numero, ou NULL se o texto acabou.
 */
static const char *le_inteiro(const char *p, const char *fim, long *valor)
{
    while (p < fim && *p != '-' && (*p < '0' || *p > '9'))
        p++;
    if (p >= fim)
        return NULL;

    int negativo = 0;
    if (*p == '-') {
        negativo = 1;
        p++;
    }
    long x = 0;
    while (p < fim && *p >= '0' && *p <= '9') {
        x = x * 10 + (*p - '0');
        p++;
    }
    *valor = negativo ? -x : x;
    return p;
}

/*
 * Em vez de fscanf (cerca de 1 s no arquivo grande, segundo o enunciado),
 * carregamos o arquivo inteiro na memoria de uma vez e convertemos os
 * numeros a mao. Cada segundo economizado aqui vira tempo de resfriamento.
 */
int le_arestas(const char *caminho, int *n, int *m,
               int **u, int **v, signed char **w)
{
    FILE *arquivo = fopen(caminho, "rb");
    if (arquivo == NULL)
        return 1;
    fseek(arquivo, 0, SEEK_END);
    long tamanho = ftell(arquivo);
    fseek(arquivo, 0, SEEK_SET);
    char *texto = aloca((size_t)tamanho + 1);
    if (fread(texto, 1, (size_t)tamanho, arquivo) != (size_t)tamanho) {
        fclose(arquivo);
        free(texto);
        return 1;
    }
    fclose(arquivo);

    const char *p = texto;
    const char *fim = texto + tamanho;
    long vn, vm;
    if ((p = le_inteiro(p, fim, &vn)) == NULL ||
        (p = le_inteiro(p, fim, &vm)) == NULL || vn < 1 || vm < 0) {
        free(texto);
        return 1;
    }

    *u = aloca((size_t)vm * sizeof(int));
    *v = aloca((size_t)vm * sizeof(int));
    *w = aloca((size_t)vm * sizeof(signed char));

    int lidas = 0;
    for (long e = 0; e < vm; e++) {
        long a, b, peso;
        if ((p = le_inteiro(p, fim, &a)) == NULL ||
            (p = le_inteiro(p, fim, &b)) == NULL ||
            (p = le_inteiro(p, fim, &peso)) == NULL)
            break;                       /* arquivo com menos linhas que o prometido */
        if (a < 1 || a > vn || b < 1 || b > vn) {
            free(texto);
            return 1;
        }
        if (a == b)
            continue;                    /* laco: nunca cruza o corte, pode sair */
        (*u)[lidas] = (int)a - 1;        /* o arquivo numera de 1 a n; aqui, de 0 a n-1 */
        (*v)[lidas] = (int)b - 1;
        (*w)[lidas] = (signed char)peso;
        lidas++;
    }
    free(texto);

    *n = (int)vn;
    *m = lidas;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Montagem                                                            */
/* ------------------------------------------------------------------ */

/* Cria ini, viz, peso e grau_max de g a partir da lista de arestas. */
static void monta_csr(Grafo *g, int n, int m,
                      const int *u, const int *v, const signed char *w)
{
    g->n = n;
    g->m = m;
    g->ini = aloca((size_t)(n + 1) * sizeof(int));
    g->viz = aloca((size_t)2 * m * sizeof(int));
    g->peso = aloca((size_t)2 * m * sizeof(signed char));

    /* 1) conta quantos vizinhos cada vertice tem */
    int *grau = aloca((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++)
        grau[i] = 0;
    for (int e = 0; e < m; e++) {
        grau[u[e]]++;
        grau[v[e]]++;
    }

    /* 2) o trecho de cada vertice comeca onde termina o do anterior */
    g->ini[0] = 0;
    g->grau_max = 0;
    for (int i = 0; i < n; i++) {
        g->ini[i + 1] = g->ini[i] + grau[i];
        if (grau[i] > g->grau_max)
            g->grau_max = grau[i];
    }

    /* 3) preenche: grau[i] vira "proxima posicao livre no trecho de i" */
    for (int i = 0; i < n; i++)
        grau[i] = g->ini[i];
    for (int e = 0; e < m; e++) {
        int k = grau[u[e]]++;
        g->viz[k] = v[e];
        g->peso[k] = w[e];
        k = grau[v[e]]++;
        g->viz[k] = u[e];
        g->peso[k] = w[e];
    }
    free(grau);
}

/*
 * Coloracao gulosa: percorre os vertices em ordem e da a cada um a menor
 * cor que nenhum vizinho ja colorido esteja usando. Garante que vizinhos
 * nunca tem a mesma cor, que e tudo o que a varredura paralela precisa.
 * Devolve quantas cores foram usadas (no maximo grau_max + 1).
 */
static int colore(const Grafo *g, int *cor)
{
    /* usada_por[c] == i quer dizer "a cor c ja esta com um vizinho de i" */
    int *usada_por = aloca((size_t)(g->grau_max + 2) * sizeof(int));
    for (int c = 0; c < g->grau_max + 2; c++)
        usada_por[c] = -1;

    int num_cores = 0;
    for (int i = 0; i < g->n; i++) {
        for (int k = g->ini[i]; k < g->ini[i + 1]; k++) {
            int j = g->viz[k];
            if (j < i)                   /* so os vizinhos que ja receberam cor */
                usada_por[cor[j]] = i;
        }
        int c = 0;
        while (usada_por[c] == i)
            c++;
        cor[i] = c;
        if (c + 1 > num_cores)
            num_cores = c + 1;
    }
    free(usada_por);
    return num_cores;
}

void monta_grafo(Grafo *g, int n, int m, int *u, int *v, const signed char *w)
{
    /* Passo 1: CSR provisorio, na numeracao do arquivo, so para colorir. */
    Grafo provisorio;
    monta_csr(&provisorio, n, m, u, v, w);

    int *cor = aloca((size_t)n * sizeof(int));
    int num_cores = colore(&provisorio, cor);

    /* Passo 2: conta os vertices de cada cor para saber onde cada cor comeca. */
    int *ini_cor = aloca((size_t)(num_cores + 1) * sizeof(int));
    for (int c = 0; c <= num_cores; c++)
        ini_cor[c] = 0;
    for (int i = 0; i < n; i++)
        ini_cor[cor[i] + 1]++;
    for (int c = 0; c < num_cores; c++)
        ini_cor[c + 1] += ini_cor[c];

    /* Passo 3: numero novo de cada vertice = proxima vaga no trecho da sua cor. */
    int *novo = aloca((size_t)n * sizeof(int));
    int *original = aloca((size_t)n * sizeof(int));
    int *vaga = aloca((size_t)num_cores * sizeof(int));
    for (int c = 0; c < num_cores; c++)
        vaga[c] = ini_cor[c];
    for (int i = 0; i < n; i++) {
        novo[i] = vaga[cor[i]]++;
        original[novo[i]] = i;
    }

    /* Passo 4: reescreve as arestas na numeracao nova e monta o CSR definitivo. */
    for (int e = 0; e < m; e++) {
        u[e] = novo[u[e]];
        v[e] = novo[v[e]];
    }
    monta_csr(g, n, m, u, v, w);
    g->num_cores = num_cores;
    g->ini_cor = ini_cor;
    g->original = original;

    free(provisorio.ini);
    free(provisorio.viz);
    free(provisorio.peso);
    free(cor);
    free(novo);
    free(vaga);
}

void libera_grafo(Grafo *g)
{
    free(g->ini);
    free(g->viz);
    free(g->peso);
    free(g->ini_cor);
    free(g->original);
}
