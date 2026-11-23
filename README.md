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
| `ferramentas/forca_bruta.py` | corte máximo exato para grafos de até 22 vértices |
| `ferramentas/referencia.c` | Algoritmo 1 sequencial, para comparação |
| `tf-maxcut/` | material oficial do professor: `gerador`, `verificador` e instâncias de exemplo em `entradas/` |
| `cluster/mede.sh` | roteiro de medição no cluster (instruções em `cluster/LEIAME.md`) |
| `cluster/fixa.sh` | prende cada rank em um conjunto próprio de núcleos |

A entrega é `src/` e o `Makefile`. O restante é apoio para testar. As instâncias e o verificador são os **oficiais** (pasta `tf-maxcut`), para comparar com os valores de referência do professor.

## Compilar e executar

```sh
make                                 # compila o maxcut
make -C tf-maxcut                    # compila o gerador e o verificador oficiais
export OMP_WAIT_POLICY=active
OMP_NUM_THREADS=4 mpirun -np 2 ./maxcut tf-maxcut/entradas/n100000_s1.txt saida.txt
tf-maxcut/verificador tf-maxcut/entradas/n100000_s1.txt saida.txt
```

As instâncias oficiais ficam em `tf-maxcut/entradas/` e podem ser recriadas com `tf-maxcut/gerador <n> <semente> > grafo.txt`.

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

Roda o programa de verdade e confere com o **verificador oficial** (compila as ferramentas de `tf-maxcut` sozinho):

1. o exemplo do enunciado dá corte 3;
2. vértices sem aresta aparecem na saída;
3. o corte é igual ao ótimo da força bruta em 10 grafos de 16 vértices (gerador oficial);
4. com 4 threads, o corte acompanhado pelo programa bate com o verificador;
5. o corte na instância Treino (`n1000_s1`) passa de 1.150 (melhor conhecido 1.178);
6. com 2 ranks e 2 threads, o resultado continua consistente;
7. o programa termina no tempo pedido.

## Resultados medidos

Tudo abaixo foi medido em um MacBook com Apple M4 (4 núcleos rápidos e 6 econômicos), não no cluster. Os números do cluster serão diferentes.

**Validação contra as ferramentas oficiais.** O corte do nosso programa confere com o `tf-maxcut/verificador` em todas as instâncias de exemplo, e iguala ou supera o melhor corte conhecido (execuções de 0,2 s a 20 s, uma máquina só):

| Instância | Melhor conhecido | Nosso corte |
|---|---:|---:|
| `exemplo_n5` | 3 (ótimo) | 3 |
| `n20_s1` | 18 (ótimo) | 18 |
| `n100_s1` | 118 | 118 |
| `n1000_s1` (Treino) | 1.178 | 1.178 |
| `n20000_s1` | 23.668 | **23.716** |
| `n100000_s1` | 116.548 | **116.890** |

Nas duas menores chegamos ao ótimo provado por força bruta; nas duas maiores (do tamanho da competição) superamos o *Simulated Annealing* de referência do professor.

**Correção**

- Operação atômica: ao removê-la de propósito, a conferência interna acusou 34 mil Δ errados com 8 threads; com ela, nenhum.
- Grafo G1 do G-set (800 vértices, melhor corte conhecido 11.624): 11.624 em duas de três execuções de 5 s (uma thread).
- Linux (máquina de entrada do LabP2D: Ubuntu 22.04, gcc 11.4, Open MPI 4.1.2): compila sem avisos, com `-march=native` e com `ARQ=x86-64-v3`, e todos os testes passam.

**Referência sequencial** (nosso Algoritmo 1, instância `n100000_s1`)

| Varreduras | Corte | Tempo |
|---|---|---|
| 100 | 114.101 | 0,07 s |
| 1.000 | 115.844 | 0,63 s |
| 10.000 | 116.432 | 6,4 s |

O ganho por década de varreduras (1.743 e 588) tem a mesma forma da Tabela II do enunciado: cada dez vezes mais varreduras rende cada vez menos.

**Varreduras em 4 s, um rank** (instâncias oficiais)

| Threads | Grande (`n100000_s1`) | Média (`n20000_s1`) |
|---|---|---|
| 1 | 12.945 | 74.268 |
| 2 | 22.587 | 63.671 |
| 4 | 32.854 | 74.199 |

Na instância grande, 4 threads fazem ~2,5 vezes mais varreduras. Na média, as threads não aceleram: cada cor tem pouco trabalho e, em temperatura alta, as threads disputam a memória dos Δ.

Sobre os parâmetros: começar em T0 = 1,5 (em vez de 1,0) melhora o corte; variar T1 entre 0,1 e 0,3 não fez diferença mensurável. Trocar a partição entre ranks durante o resfriamento não mudou o corte além do ruído, por isso o padrão é trocar pouco (`MAXCUT_TROCA=20`).

## O que falta fazer no cluster

Nada foi executado na Babitonga. O roteiro `cluster/mede.sh` (um comando, cerca de 12 minutos em um nó) coleta o que falta decidir:

- se `make` compila com o gcc e o MPI instalados, e se os testes passam;
- quantos ranks e quantas threads dão o melhor corte nas duas instâncias (um nó tem 8 núcleos rápidos e 8 econômicos);
- se um `mpirun -np N` simples funciona dentro do SLURM ou se precisa de opções;
- se `-march=native` rende mais que `-march=x86-64-v3`. A segunda opção é mais segura quando se compila em uma máquina e se executa em outra;
- o tempo de relógio de uma execução inteira, incluindo o lançamento pelo `mpirun`, para ajustar `MAXCUT_TEMPO` se a folga de 10 s for grande ou pequena demais.

Com esses números se define o comando final de execução.
