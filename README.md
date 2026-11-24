# Max-Cut com Simulated Annealing (MPI + OpenMP)

Trabalho final de Programação Paralela. O programa recebe um grafo com pesos +1/-1 e procura uma partição dos vértices que maximize o corte.

## Compilar

Precisa de g++ com OpenMP e de uma implementação de MPI (Open MPI) com `mpicxx`.

```
make
```

Gera o executável `./maxcut`. `make clean` apaga o executável e a pasta `build/`.

## Executar

```
mpirun -np 4 ./maxcut <grafo.txt> <saida.txt>
```

No cluster:

```
mpirun --mca btl_tcp_if_include ip/mascara -np N --machinefile hosts.txt ./maxcut <grafo.txt> <saida.txt>
```

### Entrada e saída

- Entrada: primeira linha `n m`; depois `m` linhas `u v w`, com vértices de 1 a n e `w` igual a 1 ou -1.
- Saída: uma linha com `n` caracteres `0` ou `1`, o lado de cada vértice.

### Opções

| Opção | O que faz |
| --- | --- |
| `--tempo S` | tempo total do programa em segundos|
| `--varreduras N` | roda N varreduras em vez de usar o tempo |
| `--seed N` | semente aleatória |
| `--t0 X`, `--t1 X` | temperatura inicial (padrão 2.0) e final (padrão 0.15) |
| `--threads N` | threads OpenMP por processo (padrão: as CPUs do processo) |
| `--varredura sequencial\|colorida` | tipo de varredura (padrão: colorida se houver mais de 1 thread) |
| `--ponto S` | grava a melhor solução na saída a cada S segundos (padrão 5; 0 grava só no fim) |
| `--trace ARQ.csv` | grava a trajetória do SA |

O verificador de `aux/` calcula o corte de uma solução:

```
make build/verificador
./build/verificador <grafo.txt> <saida.txt>
```
