# Medição na Babitonga

Este roteiro compila o programa em um nó do cluster, roda os testes e mede o desempenho com várias combinações de ranks MPI e threads. O resultado sai em um único arquivo de texto.

Ele ocupa **um nó inteiro, com exclusividade, por cerca de 12 minutos**. Não usa GPU nem instala nada.

## Passo a passo

1. Copie a pasta do projeto para a sua área em `/mnt/prj/<seu-usuário>/`, que é a pasta que os nós enxergam. Se recebeu o arquivo `maxcut-cluster.tar.gz`:

   ```sh
   cd /mnt/prj/$USER
   tar xzf maxcut-cluster.tar.gz
   cd maxcut
   ```

2. Na `baia0`, a partir da raiz do projeto, rode:

   ```sh
   bash cluster/mede.sh
   ```

   O roteiro envia a si mesmo para a fila do SLURM e volta na hora.

3. Acompanhe com `squeue -u $USER`. Quando o trabalho sumir da fila, confira que a última linha do resultado é `FIM DA MEDICAO`:

   ```sh
   tail -1 cluster/resultado-*.txt
   ```

4. Envie de volta o arquivo `cluster/resultado-*.txt`. Se ele não existir ou estiver incompleto, envie também o `slurm-*.out` que aparece na raiz do projeto.

## Se algo der errado

- **O trabalho fica parado na fila (`PD`).** O roteiro pede um nó exclusivo; ele só começa quando algum nó estiver vazio. `sinfo` mostra quais estão livres.
- **`sbatch` recusa o envio.** Mande a mensagem de erro; o resto não depende de você.
- **Quer rodar mais rápido.** `DURACAO=5 REPETICOES=1 bash cluster/mede.sh` leva uns 4 minutos, com números menos precisos.

## O que o arquivo de resultado contém

- processador, gcc, MPI e variáveis do SLURM do nó;
- se `mpirun -np N` funciona sem opções extras;
- a saída da compilação e dos testes;
- uma tabela com varreduras por execução, corte obtido e tempo de relógio para cada combinação de ranks e threads, nas instâncias de 20 mil e de 100 mil vértices;
- uma execução completa, com o tempo padrão, para conferir que termina antes de 90 s.
