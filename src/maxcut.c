/*
 * Max-Cut com Simulated Annealing paralelo: OpenMP + MPI.
 *
 * Uso: mpirun -np <ranks> ./maxcut <arquivo_do_grafo> <arquivo_da_solucao>
 *
 * Duas camadas de paralelismo:
 *
 *  - OpenMP, DENTRO de uma cadeia: os vertices foram coloridos de modo que
 *    vizinhos nunca tenham a mesma cor. As threads processam juntas todos os
 *    vertices de uma cor, esperam na barreira e passam para a cor seguinte.
 *    Isso faz a cadeia varrer o grafo mais vezes no mesmo tempo.
 *
 *  - MPI, ENTRE cadeias: cada rank roda a sua propria cadeia, com sementes
 *    diferentes. De tempos em tempos todos adotam a particao do rank que
 *    esta com o maior corte, e no final o melhor resultado e gravado.
 *
 * Representacao: lado[i] vale +1 (grupo A) ou -1 (grupo B). Com isso o
 * produto lado[i] * lado[j] vale +1 se i e j estao juntos e -1 se estao
 * separados, e as contas de delta nao precisam de "if".
 */
#include <math.h>
#include <mpi.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "grafo.h"

/* Duracao desejada de um bloco de varreduras, em segundos. Entre um bloco e
 * outro o programa olha o relogio e baixa a temperatura. */
#define ALVO_BLOCO 0.002

/* O estado de uma cadeia. */
typedef struct {
    signed char *lado;  /* +1 = grupo A, -1 = grupo B */
    int *delta;         /* delta[i] = quanto o corte muda se i trocar de lado */
    int corte;          /* valor do corte da particao atual */
} Estado;

/*
 * Gerador de numeros aleatorios de uma thread. Cada thread tem o seu, para
 * nao disputarem o mesmo. O enchimento faz cada gerador ocupar uma linha de
 * cache inteira (64 bytes): sem ele, dois geradores vizinhos cairiam na
 * mesma linha e as threads atrapalhariam uma a outra a cada sorteio
 * ("false sharing").
 */
typedef struct {
    uint64_t s;
    char enchimento[56];
} Gerador;

/* ------------------------------------------------------------------ */
/* Numeros aleatorios                                                  */
/* ------------------------------------------------------------------ */

/* Embaralha os bits de x (splitmix64). Usado so para criar sementes bem
 * diferentes entre si a partir de numeros parecidos (rank 0, 1, 2...). */
static uint64_t mistura(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* Proximo numero aleatorio de 64 bits (xorshift64*): tres deslocamentos e
 * uma multiplicacao, bem mais barato que rand(). */
static inline uint64_t sorteia(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* ------------------------------------------------------------------ */
/* Contas sobre o estado                                               */
/* ------------------------------------------------------------------ */

/*
 * Calcula do zero todos os deltas e o corte a partir de lado[].
 * Usado no inicio e sempre que a cadeia recebe a particao de outro rank.
 */
static void recalcula(const Grafo *g, Estado *e)
{
    long dobro_do_corte = 0;
    #pragma omp parallel for schedule(static) reduction(+ : dobro_do_corte)
    for (int i = 0; i < g->n; i++) {
        int d = 0;
        for (int k = g->ini[i]; k < g->ini[i + 1]; k++) {
            /* junto = +1 se o vizinho esta do mesmo lado, -1 se nao */
            int junto = e->lado[i] * e->lado[g->viz[k]];
            d += g->peso[k] * junto;       /* mesmo lado: +w; outro lado: -w */
            if (junto < 0)
                dobro_do_corte += g->peso[k];
        }
        e->delta[i] = d;
    }
    /* cada aresta que cruza foi contada duas vezes, uma em cada ponta */
    e->corte = (int)(dobro_do_corte / 2);
}

/*
 * Tabela de aceitacao para a temperatura T.
 *
 * Uma troca que piora o corte em k pontos (delta = -k) e aceita com
 * probabilidade exp(-k / T). Em vez de chamar exp() para cada vertice,
 * calculamos uma vez por bloco o limiar de cada k possivel, ja na escala
 * de um sorteio de 32 bits: a troca e aceita se sorteio < limiar[k].
 * Como delta nunca passa do grau do vertice, a tabela tem grau_max + 1 casas.
 * Com T = 0 a tabela fica toda zerada: nenhuma troca que piora e aceita.
 */
static void preenche_limiares(uint32_t *limiar, int grau_max, double T)
{
    for (int k = 0; k <= grau_max; k++) {
        double p = (T > 0.0) ? exp(-k / T) : 0.0;
        double escala = p * 4294967296.0;          /* 2^32 */
        limiar[k] = (escala >= 4294967295.0) ? 4294967295u : (uint32_t)escala;
    }
}

/*
 * O coracao do programa: faz `varreduras` varreduras completas do grafo e
 * devolve o quanto o corte mudou.
 *
 * aceita_direto diz a partir de qual delta a troca e aceita sem sorteio:
 *   0 no resfriamento (aceita delta >= 0, como no Algoritmo 1);
 *   1 na descida final (so aceita o que melhora de verdade).
 */
static long varre(const Grafo *g, Estado *e, const uint32_t *limiar,
                  int aceita_direto, int varreduras, Gerador *geradores)
{
    const int *ini = g->ini;
    const int *viz = g->viz;
    const signed char *peso = g->peso;
    signed char *lado = e->lado;
    int *delta = e->delta;
    long ganho = 0;

    /* A regiao paralela engloba todas as varreduras do bloco, para as
     * threads serem criadas uma vez so. reduction(+): cada thread soma o
     * seu ganho numa copia particular e o OpenMP junta tudo no final. */
    #pragma omp parallel reduction(+ : ganho)
    {
        /* copia local do gerador desta thread; devolvida ao final */
        uint64_t semente = geradores[omp_get_thread_num()].s;

        for (int v = 0; v < varreduras; v++) {
            for (int c = 0; c < g->num_cores; c++) {
                /* Reparte os vertices da cor c entre as threads. Ao final
                 * do laco ha uma barreira implicita: ninguem comeca a cor
                 * seguinte antes de todos terminarem esta. */
                #pragma omp for schedule(static)
                for (int i = g->ini_cor[c]; i < g->ini_cor[c + 1]; i++) {
                    int d = delta[i];

                    if (d < aceita_direto) {
                        /* troca que nao melhora: so passa no sorteio */
                        uint32_t r = (uint32_t)(sorteia(&semente) >> 32);
                        if (r >= limiar[-d])
                            continue;                   /* rejeitada */
                    }

                    /* aceita: troca o vertice de lado */
                    signed char s = (signed char)-lado[i];
                    lado[i] = s;
                    delta[i] = -d;
                    ganho += d;

                    /* Corrige o delta de cada vizinho. A aresta i-j mudou
                     * de situacao, entao a parcela dela no delta de j
                     * inverteu de sinal: passou a valer peso * s * lado[j],
                     * e a diferenca e o dobro disso.
                     * Dois vertices desta cor podem ter o MESMO vizinho j,
                     * por isso a soma e atomica. */
                    for (int k = ini[i]; k < ini[i + 1]; k++) {
                        int j = viz[k];
                        int ajuste = 2 * peso[k] * s * lado[j];
                        #pragma omp atomic
                        delta[j] += ajuste;
                    }
                }
            }
        }

        geradores[omp_get_thread_num()].s = semente;
    }
    return ganho;
}

/*
 * Descida final: troca todo vertice que melhora o corte, ate nao sobrar
 * nenhum. E a "busca local simples" que o enunciado permite. Termina
 * porque cada troca aumenta o corte em pelo menos 1.
 */
static void descida(const Grafo *g, Estado *e, uint32_t *limiar, Gerador *geradores)
{
    preenche_limiares(limiar, g->grau_max, 0.0);
    long ganho;
    do {
        ganho = varre(g, e, limiar, 1, 1, geradores);
        e->corte += (int)ganho;
    } while (ganho > 0);
}

/*
 * Conferencia (so com MAXCUT_CHECA=1): recalcula tudo do zero e compara com
 * os valores que a cadeia vinha atualizando aos poucos. Se alguma soma de
 * delta se perdeu numa disputa entre threads, aparece aqui.
 */
static void confere(const Grafo *g, const Estado *e, int rank)
{
    Estado certo;
    certo.lado = e->lado;
    certo.delta = malloc((size_t)g->n * sizeof(int));
    recalcula(g, &certo);

    int erros = 0;
    for (int i = 0; i < g->n; i++)
        if (certo.delta[i] != e->delta[i])
            erros++;
    if (erros > 0 || certo.corte != e->corte) {
        fprintf(stderr, "maxcut: ERRO no rank %d: %d deltas errados, "
                        "corte acompanhado %d, corte real %d\n",
                rank, erros, e->corte, certo.corte);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    free(certo.delta);
}

/*
 * Troca entre cadeias: descobre qual rank tem a maior nota e todos passam
 * a usar a particao dele. A nota e passada por quem chama:
 *  - durante o resfriamento, a MEDIA do corte desde a ultima troca. O corte
 *    de um instante oscila bastante com a temperatura; a media mostra melhor
 *    qual cadeia esta de fato numa regiao melhor;
 *  - na reducao final, o proprio corte.
 */
static void adota_melhor(const Grafo *g, Estado *e, int rank, double nota)
{
    /* MPI_MAXLOC devolve o maior valor e de quem ele e (par valor, rank). */
    struct { double valor; int rank; } meu = { nota, rank }, vencedor;
    MPI_Allreduce(&meu, &vencedor, 1, MPI_DOUBLE_INT, MPI_MAXLOC, MPI_COMM_WORLD);

    /* O vencedor envia a particao; os outros recebem por cima da propria. */
    MPI_Bcast(e->lado, g->n, MPI_SIGNED_CHAR, vencedor.rank, MPI_COMM_WORLD);
    if (rank != vencedor.rank)
        recalcula(g, e);        /* particao nova: deltas e corte do zero */
}

/* ------------------------------------------------------------------ */
/* Programa principal                                                  */
/* ------------------------------------------------------------------ */

/* Le um parametro numerico de uma variavel de ambiente (para experimentos). */
static double parametro(const char *nome, double padrao)
{
    const char *texto = getenv(nome);
    return (texto != NULL && texto[0] != '\0') ? atof(texto) : padrao;
}

int main(int argc, char **argv)
{
    /* FUNNELED: ha threads, mas so a thread principal chama o MPI (todas as
     * chamadas ficam fora das regioes paralelas). */
    int suporte;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &suporte);
    double t0 = MPI_Wtime();

    int rank, ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);

    if (argc != 3) {
        if (rank == 0)
            fprintf(stderr, "uso: %s <arquivo_do_grafo> <arquivo_da_solucao>\n", argv[0]);
        MPI_Finalize();
        return 2;
    }

    /* ---- Parametros: o rank 0 le e distribui, para todos usarem os mesmos ---- */
    double par[5];
    if (rank == 0) {
        par[0] = parametro("MAXCUT_TEMPO", 80.0);  /* segundo em que o resfriamento termina */
        par[1] = parametro("MAXCUT_T0", 1.5);      /* temperatura inicial */
        par[2] = parametro("MAXCUT_T1", 0.3);      /* temperatura final */
        /* Segundos entre trocas (0 = so a reducao final). Nos testes de 10 s
         * com 4 ranks, trocar a cada 0,3 s, 1 s ou 2,5 s deu o mesmo corte
         * que nao trocar, dentro do ruido; 20 s da poucas trocas por execucao. */
        par[3] = parametro("MAXCUT_TROCA", 20.0);
        par[4] = parametro("MAXCUT_CHECA", 0.0);   /* 1 = liga a conferencia interna */
    }
    MPI_Bcast(par, 5, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    double fim = par[0], T0 = par[1], T1 = par[2], periodo = par[3];
    int checa = par[4] != 0.0;

    /* ---- Grafo: o rank 0 le o arquivo e envia as arestas aos outros ---- */
    int n = 0, m = 0;
    int *u = NULL, *v = NULL;
    signed char *w = NULL;
    if (rank == 0 && le_arestas(argv[1], &n, &m, &u, &v, &w) != 0) {
        fprintf(stderr, "maxcut: nao consegui ler %s\n", argv[1]);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    int tamanho[2] = { n, m };
    MPI_Bcast(tamanho, 2, MPI_INT, 0, MPI_COMM_WORLD);
    n = tamanho[0];
    m = tamanho[1];
    if (rank != 0) {
        u = malloc((size_t)(m + 1) * sizeof(int));
        v = malloc((size_t)(m + 1) * sizeof(int));
        w = malloc((size_t)(m + 1));
    }
    MPI_Bcast(u, m, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(v, m, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(w, m, MPI_SIGNED_CHAR, 0, MPI_COMM_WORLD);

    /* Cada rank monta o seu grafo. O resultado e identico em todos, entao
     * a particao de um rank faz sentido para os outros. */
    Grafo g;
    monta_grafo(&g, n, m, u, v, w);
    free(u);
    free(v);
    free(w);

    /* ---- Geradores aleatorios: um por thread, diferentes em cada rank e
     *      em cada execucao (a semente e livre, segundo o enunciado) ---- */
    int threads = omp_get_max_threads();
    Gerador *geradores = malloc((size_t)threads * sizeof(Gerador));
    uint64_t base = (uint64_t)time(NULL) * 1000003ULL + (uint64_t)getpid();
    for (int t = 0; t < threads; t++)
        geradores[t].s = mistura(base + (uint64_t)rank * 7919ULL + (uint64_t)t * 104729ULL) | 1;

    /* ---- Estado inicial: cada vertice num lado sorteado ---- */
    Estado est;
    est.lado = malloc((size_t)n);
    est.delta = malloc((size_t)n * sizeof(int));
    uint64_t semente = mistura(base ^ ((uint64_t)(rank + 1) << 32)) | 1;
    for (int i = 0; i < n; i++)
        est.lado[i] = (sorteia(&semente) >> 63) ? 1 : -1;
    recalcula(&g, &est);

    /* A melhor particao que esta cadeia ja viu ao final de um bloco. */
    signed char *melhor_lado = malloc((size_t)n);
    int melhor_corte = est.corte;
    memcpy(melhor_lado, est.lado, (size_t)n);

    uint32_t *limiar = malloc((size_t)(g.grau_max + 1) * sizeof(uint32_t));

    /* Quantas trocas entre ranks vao acontecer: nos instantes periodo,
     * 2*periodo, ... antes de `fim`. Todos os ranks calculam o mesmo numero,
     * o que garante que ninguem fica esperando uma troca que nao vem. */
    int total_trocas = 0;
    if (ranks > 1 && periodo > 0.0)
        while ((total_trocas + 1) * periodo < fim)
            total_trocas++;
    int trocas_feitas = 0;
    double soma_periodo = 0.0;           /* soma dos cortes vistos desde a ultima troca */
    long blocos_periodo = 0;             /* quantos blocos entraram nessa soma */

    /* ---- Resfriamento ---- */
    double inicio_resf = MPI_Wtime() - t0;
    long total_varreduras = 0;
    int varreduras = 1;                  /* varreduras por bloco; se ajusta sozinho */

    for (;;) {
        double agora = MPI_Wtime() - t0;
        if (agora >= fim)
            break;

        /* A temperatura segue o RELOGIO, nao o numero da varredura: comeca
         * em T0, termina em T1 exatamente quando o tempo acaba, em qualquer
         * maquina. E a formula do Algoritmo 1 com s/(S-1) trocado pela
         * fracao do tempo ja gasta. */
        double fracao = (agora - inicio_resf) / (fim - inicio_resf);
        double T = T0 * pow(T1 / T0, fracao);
        preenche_limiares(limiar, g.grau_max, T);

        est.corte += (int)varre(&g, &est, limiar, 0, varreduras, geradores);
        total_varreduras += varreduras;

        if (est.corte > melhor_corte) {
            melhor_corte = est.corte;
            memcpy(melhor_lado, est.lado, (size_t)n);
        }
        soma_periodo += est.corte;
        blocos_periodo++;

        /* Ajusta o tamanho do proximo bloco para durar perto de ALVO_BLOCO. */
        double depois = MPI_Wtime() - t0;
        double por_varredura = (depois - agora) / varreduras;
        double ideal = (por_varredura > 0.0) ? ALVO_BLOCO / por_varredura : 1000.0;
        varreduras = (ideal < 1.0) ? 1 : (ideal > 100000.0) ? 100000 : (int)ideal;

        /* Chegou a hora de uma troca entre ranks? */
        while (trocas_feitas < total_trocas &&
               depois >= (trocas_feitas + 1) * periodo) {
            double media = (blocos_periodo > 0) ? soma_periodo / blocos_periodo : est.corte;
            adota_melhor(&g, &est, rank, media);
            trocas_feitas++;
            soma_periodo = 0.0;
            blocos_periodo = 0;
        }
    }
    /* Seguranca: todo rank faz exatamente total_trocas trocas. */
    while (trocas_feitas < total_trocas) {
        adota_melhor(&g, &est, rank, est.corte);
        trocas_feitas++;
    }

    if (checa)
        confere(&g, &est, rank);

    /* ---- Acabamento: descida final e escolha da melhor particao vista ---- */
    descida(&g, &est, limiar, geradores);
    if (est.corte < melhor_corte) {
        /* a melhor particao guardada era melhor que a final: volta para ela */
        memcpy(est.lado, melhor_lado, (size_t)n);
        recalcula(&g, &est);
        descida(&g, &est, limiar, geradores);
    }
    if (checa)
        confere(&g, &est, rank);

    /* ---- Reducao final: todos ficam com a particao do melhor rank ---- */
    adota_melhor(&g, &est, rank, est.corte);

    if (rank == 0) {
        /* Volta para a numeracao do arquivo: '0' = grupo A, '1' = grupo B. */
        char *saida = malloc((size_t)n + 1);
        for (int i = 0; i < n; i++)
            saida[g.original[i]] = (est.lado[i] > 0) ? '0' : '1';
        saida[n] = '\n';

        FILE *arquivo = fopen(argv[2], "wb");
        if (arquivo == NULL || fwrite(saida, 1, (size_t)n + 1, arquivo) != (size_t)n + 1) {
            fprintf(stderr, "maxcut: nao consegui gravar %s\n", argv[2]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fclose(arquivo);
        free(saida);

        fprintf(stderr, "maxcut: n=%d m=%d cores=%d ranks=%d threads=%d "
                        "varreduras=%ld corte=%d tempo=%.2fs\n",
                n, m, g.num_cores, ranks, threads, total_varreduras,
                est.corte, MPI_Wtime() - t0);
    }

    free(geradores);
    free(est.lado);
    free(est.delta);
    free(melhor_lado);
    free(limiar);
    libera_grafo(&g);
    MPI_Finalize();
    return 0;
}
