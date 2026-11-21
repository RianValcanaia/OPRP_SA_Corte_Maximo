/*
    mpi_util.hpp — o que o programa precisa do MPI, em poucas funções.

    O mesmo binário roda com ou sem mpirun (sem mpirun é um "mundo" de 1 rank). O MPI é usado
    só para: dividir as CPUs do nó entre os ranks, mandar o grafo do rank 0 aos demais, combinar
    o prazo, e no fim escolher a melhor partição e entregá-la ao rank 0, que grava a saída.
    Durante o SA os ranks não se falam.

    Todas as funções coletivas devem ser chamadas por TODOS os ranks, na mesma ordem.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grafo.hpp"

namespace dist {

    struct Ambiente {
        int rank = 0;               // meu número em [0, tamanho)
        int tamanho = 1;            // total de ranks
        int tamanhoLocal = 1;       // quantos ranks rodam neste mesmo nó
        int cpusPorRank = 1;        // CPUs que este rank pode usar (as do nó divididas entre os ranks do nó)
    };

    // MPI_Init_thread (MPI_THREAD_FUNNELED: só a thread principal chama o MPI, as do OpenMP não) e
    // descoberta de quantos ranks dividem o nó. Se o mpirun prendeu o rank a poucos núcleos
    // (o Open MPI prende cada rank a 1 núcleo com -np <= 2), libera o processo para todas as CPUs do nó.
    Ambiente iniciar(int &argc, char **&argv);

    void finalizar();

    // O rank 0 passa a mensagem de erro da leitura do grafo ("" se leu bem) e o grafo lido; os outros
    // recebem o grafo. Devolve "" se deu tudo certo, ou a mensagem de erro (igual em todos os ranks).
    std::string difundirGrafo(const Ambiente &amb, grafo::Grafo &g, const std::string &erroDaLeitura);

    // Maior valor entre os ranks (todos recebem o resultado).
    double maximoEntreRanks(double valor);

    // O valor do rank 0 vale para todos.
    uint64_t valorDoRank0(uint64_t valor);

    struct MelhorCorte {
        int64_t corte;
        int rank;       // quem tem esse corte (em empate, o menor rank)
    };

    // Melhor corte entre os ranks (MPI_Allreduce com MAXLOC).
    MelhorCorte melhorEntreRanks(int64_t corteLocal);

    // O rank 'origem' manda a partição ao rank 0, que a recebe em 'particao'. Se origem == 0, não faz nada.
    void enviarParticaoAoRank0(const Ambiente &amb, int origem, std::vector<uint8_t> &particao);

    // O corte de cada rank, reunido no rank 0 (vetor vazio nos outros ranks).
    std::vector<int64_t> reunirCortes(const Ambiente &amb, int64_t corteLocal);

}  // namespace dist
