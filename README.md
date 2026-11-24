# Max-Cut com Simulated Annealing (MPI + OpenMP)

Trabalho final de Programação Paralela. O programa recebe um grafo com pesos +1/-1 e procura uma partição dos vértices que maximize o corte.

## Compilar

Precisa de g++ com OpenMP e de uma implementação de MPI (Open MPI) com `mpicxx`.

```
make
```

Gera os executáveis `./maxcut` e `./competicao`. `make maxcut` ou `make competicao` compilam só um deles. `make clean` apaga os executáveis e a pasta `build/`.

O programa é executado no cluster. Lista das máquinas (uma por linha) em `hosts.txt`.

## Versão da competição

Roda 90 s, escolhe as temperaturas pelo tamanho do grafo. Na Babitonga (7 máquinas, baia1 a baia7):

```
mpirun --map-by node --machinefile hosts.txt -np 7 ./competicao <grafo.txt> <saida.txt>
```

## Executável padrão

```
mpirun --map-by node --machinefile hosts.txt -np N ./maxcut <grafo.txt> <saida.txt>
```

`--map-by node` espalha os processos entre as máquinas (um por máquina com `-np` igual ao número de máquinas).

Opcional: se o Open MPI escolher a rede errada entre as máquinas, indique a correta com `--mca btl_tcp_if_include ip/mascara`, por exemplo:

```
mpirun --mca btl_tcp_if_include ip/mascara --map-by node --machinefile hosts.txt -np 7 ./competicao <grafo.txt> <saida.txt>
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
