# Trabalho final de Programacao Paralela: Max-Cut com Simulated Annealing
# Ferramentas: OpenMP (varredura colorida) + MPI (uma cadeia por rank)

CC     = mpicc
CFLAGS = -O3 -std=gnu11 -Wall -Wextra
LDLIBS = -lm

ifeq ($(shell uname),Darwin)
  # macOS (desenvolvimento): o clang da Apple nao traz OpenMP, vem do Homebrew.
  LIBOMP  = $(shell brew --prefix libomp)
  CFLAGS += -mcpu=native -Xclang -fopenmp -I$(LIBOMP)/include
  LDLIBS += -L$(LIBOMP)/lib -lomp
else
  # Linux (ambiente de avaliacao): gcc com OpenMP embutido.
  # ARQ diz para qual processador o gcc gera codigo:
  #   native    = o da maquina onde se compila (padrao);
  #   x86-64-v3 = qualquer Intel/AMD com AVX2. Serve quando se compila numa
  #               maquina e se executa em outra (ex.: "make ARQ=x86-64-v3").
  ARQ    ?= native
  CFLAGS += -march=$(ARQ) -fopenmp
endif

FONTES = src/maxcut.c src/grafo.c

maxcut: $(FONTES) src/grafo.h
	$(CC) $(CFLAGS) -o $@ $(FONTES) $(LDLIBS)

# Algoritmo 1 sequencial, so para comparacao (nao faz parte da entrega).
referencia: ferramentas/referencia.c src/grafo.c src/grafo.h
	cc -O3 -std=gnu11 -Wall -Wextra -o $@ ferramentas/referencia.c src/grafo.c -lm

teste: maxcut
	bash testes/testa.sh

clean:
	rm -f maxcut referencia

.PHONY: teste clean
