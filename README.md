# Max-Cut com Simulated Annealing: OpenMP + MPI

Trabalho final de Programação Paralela (UDESC, 2026/2). O programa lê um grafo com pesos +1/−1 e grava, em até 90 segundos, a melhor divisão dos vértices em dois grupos que conseguir encontrar.

## Como funciona

O algoritmo é o Simulated Annealing do enunciado (Algoritmo 1), com duas camadas de paralelismo.

**OpenMP, dentro de uma cadeia.** Os vértices são coloridos de modo que vizinhos nunca tenham a mesma cor. As threads processam juntas todos os vértices de uma cor, esperam na barreira e passam para a cor seguinte. Dois vizinhos nunca trocam de lado ao mesmo tempo, então o resultado é o mesmo de uma varredura sequencial naquela ordem.

**MPI, entre cadeias.** Cada rank roda a sua própria cadeia, com sementes diferentes. A cada 20 segundos todos adotam a partição da cadeia com a maior média de corte no período, e no final a melhor partição é gravada pelo rank 0.

Três decisões que explicam o desempenho:

- **Δ guardado.** Cada vértice guarda quanto o corte muda se ele trocar de lado. Uma troca rejeitada custa uma leitura e uma comparação; só as aceitas mexem nos vizinhos. Como dois vértices da mesma cor podem ter um vizinho em comum, essa correção usa `#pragma omp atomic`.
- **Vértices renumerados por cor.** Os vértices de uma cor ficam em posições seguidas da memória, o que facilita repartir entre as threads e aproveita o cache.
- **Temperatura guiada pelo relógio.** A temperatura cai de 1,5 para 0,3 conforme o tempo passa, e não conforme o número da varredura. O resfriamento termina na hora certa em qualquer máquina.

Ao final há uma descida: troca-se todo vértice que melhora o corte até não sobrar nenhum.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `src/maxcut.c` | Simulated Annealing, OpenMP e MPI |
| `src/grafo.c`, `src/grafo.h` | leitura do arquivo, coloração, lista de vizinhos |
| `Makefile` | compila com `mpicc` e OpenMP |
| `testes/testa.sh` | testes de validação |
| `ferramentas/verifica.py` | verificador independente: calcula o corte de uma saída |
| `ferramentas/gera.py` | gerador de grafos aleatórios no formato do enunciado |
| `ferramentas/forca_bruta.py` | corte máximo exato para grafos de até 22 vértices |
| `ferramentas/referencia.c` | Algoritmo 1 sequencial, para comparação |
| `instancias/` | grafos gerados com `gera.py` (1.000, 20.000 e 100.000 vértices, semente 1) |
| `cluster/mede.sh` | roteiro de medição no cluster (instruções em `cluster/LEIAME.md`) |
| `cluster/fixa.sh` | prende cada rank em um conjunto próprio de núcleos |

A entrega é `src/` e o `Makefile`. O restante é apoio para testar.

## Compilar e executar

```sh
make
export OMP_WAIT_POLICY=active
OMP_NUM_THREADS=4 mpirun -np 2 ./maxcut instancias/grande.txt saida.txt
python3 ferramentas/verifica.py instancias/grande.txt saida.txt
```

`OMP_WAIT_POLICY=active` faz as threads esperarem na barreira sem dormir. Sem isso, no macOS, 4 threads ficaram mais lentas que 1. O número de ranks vezes o de threads não deve passar do número de núcleos, senão a espera ocupada passa a atrapalhar.

O programa aceita ajustes por variáveis de ambiente, usadas nos experimentos:

| Variável | Padrão | Significado |
|---|---|---|
| `MAXCUT_TEMPO` | 80 | segundo, contado do início do programa, em que o resfriamento termina |
| `MAXCUT_T0` | 1.5 | temperatura inicial |
| `MAXCUT_T1` | 0.3 | temperatura final |
| `MAXCUT_TROCA` | 20 | segundos entre trocas de partição entre ranks (0 = só a redução final) |
| `MAXCUT_CHECA` | 0 | 1 = ao final, recalcula tudo do zero e compara com os valores acompanhados |

## Testes

```sh
make teste
```

Roda o programa de verdade e confere com o verificador independente:

1. o exemplo do enunciado dá corte 3;
2. vértices sem aresta aparecem na saída;
3. o corte é igual ao ótimo da força bruta em 10 grafos de 16 vértices;
4. com 4 threads, o corte acompanhado pelo programa bate com o verificador;
5. o corte no grafo de 1.000 vértices passa de um mínimo;
6. com 2 ranks e 2 threads, o resultado continua consistente;
7. o programa termina no tempo pedido.

## Resultados medidos

Tudo abaixo foi medido em um MacBook com Apple M4 (4 núcleos rápidos e 6 econômicos), não no cluster. Os números do cluster serão diferentes.

**Validação**

- Grafo G1 do G-set (800 vértices, melhor corte conhecido 11.624): 11.624, 11.598 e 11.624 em três execuções de 5 s com uma thread.
- Operação atômica: ao removê-la de propósito, a conferência interna acusou 34 mil Δ errados com 8 threads; com ela, nenhum.
- Execução completa na instância de 100 mil vértices (2 ranks, 2 threads, tempo padrão): 80,1 s de relógio, corte 117.960.

**Referência sequencial** (Algoritmo 1, instância de 100 mil vértices)

| Varreduras | Corte | Tempo |
|---|---|---|
| 100 | 115.198 | 0,08 s |
| 1.000 | 117.075 | 0,64 s |
| 10.000 | 117.545 | 6,4 s |

O ganho por década de varreduras (1.877 e 470) é parecido com o da Tabela II do enunciado (1.849 e 550).

**Varreduras em 4 s, um rank**

| Threads | Grande (100 mil) | Média (20 mil) |
|---|---|---|
| 1 | 12.991 | 75.304 |
| 2 | 22.949 | 64.188 |
| 4 | 32.975 | 74.936 |

Na instância grande, 4 threads fazem 2,5 vezes mais varreduras. Na média, as threads não aceleram: cada cor tem pouco trabalho e, em temperatura alta, as threads disputam a memória dos Δ.

**Corte em 10 s na instância grande** (média de 3 ou 4 execuções; o ruído entre execuções é de cerca de 25 pontos)

| Configuração | Corte |
|---|---|
| 1 rank, 1 thread | 117.823 |
| 4 ranks, 1 thread, sem troca | 117.855 a 117.871 |
| 4 ranks, 1 thread, com troca (0,3 s a 2,5 s) | 117.851 a 117.874 |
| 1 rank, 4 threads | 117.889 |

Começar em T0 = 1,0 em vez de 1,5 piorou o corte em cerca de 55 pontos. Variar T1 entre 0,1 e 0,3 não fez diferença mensurável.

## O que falta fazer no cluster

Nada foi executado na Babitonga. O roteiro `cluster/mede.sh` (um comando, cerca de 12 minutos em um nó) coleta o que falta decidir:

- se `make` compila com o gcc e o MPI instalados, e se os testes passam;
- quantos ranks e quantas threads dão o melhor corte nas duas instâncias (um nó tem 8 núcleos rápidos e 8 econômicos);
- se um `mpirun -np N` simples funciona dentro do SLURM ou se precisa de opções;
- se `-march=native` rende mais que `-march=x86-64-v3`. A segunda opção é mais segura quando se compila em uma máquina e se executa em outra;
- o tempo de relógio de uma execução inteira, incluindo o lançamento pelo `mpirun`, para ajustar `MAXCUT_TEMPO` se a folga de 10 s for grande ou pequena demais.

Com esses números se define o comando final de execução.
