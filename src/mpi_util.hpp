#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grafo.hpp"

namespace dist {

    struct Ambiente {
        int rank = 0;               // [0, tamanho)
        int tamanho = 1;            // total de ranks
        int tamanhoLocal = 1;       // quantos ranks rodam neste mesmo nó
        int cpusPorRank = 1;        // CPUs logicas que este rank pode usar
        int nucleosPorRank = 1;     // nucleos fisicos que este rank pode usar
    };

    struct MelhorCorte {
        int64_t corte;
        int rank;       // quem tem esse corte (em empate, o menor rank)
    };

    // prepara o MPI e devolve um ambiente com as informacoes que o resto do programa precisa
    Ambiente iniciar(int &argc, char **&argv);

    // encerra o MPI chamando MPI_Finalize()
    void finalizar();

    // entrega o grafo lido pelo rank 0 a todos os outros ranks
    std::string difundirGrafo(const Ambiente &amb, grafo::Grafo &g, const std::string &erroDaLeitura);

    // maior valor entre os ranks 
    double maximoEntreRanks(double valor);

    // O valor do rank 0 vale para todos.
    uint64_t valorDoRank0(uint64_t valor);

    // melhor corte entre os ranks
    MelhorCorte melhorEntreRanks(int64_t corteLocal);

    // o rank origem manda a particao ao rank 0, que a recebe em particao. Se origem == 0, nao faz nada
    void enviarParticaoAoRank0(const Ambiente &amb, int origem, std::vector<uint8_t> &particao);

    // o corte de cada rank, reunido no rank 0 
    std::vector<int64_t> reunirCortes(const Ambiente &amb, int64_t corteLocal);

}  // namespace dist
