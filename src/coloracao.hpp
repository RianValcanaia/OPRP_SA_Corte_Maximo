/*
    coloracao.hpp — coloração gulosa do grafo
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "grafo.hpp"

namespace coloracao {

    struct Coloracao {
        int32_t numCores = 0;
        std::vector<int32_t> cor;        // cor[v]: cor do vertice v, em [0, numCores)
        std::vector<int32_t> vertices;   // vertices agrupados por cor 
        std::vector<int32_t> inicio;     // a cor c ocupa vertices[inicio[c] .. inicio[c+1])

        // qtd de vertices com uma cor c
        int32_t tamanho(int32_t c) const { 
            return inicio[c + 1] - inicio[c]; 
        }
    };

    struct GrafoColorido {
        grafo::Grafo grafo;                // o grafo com os vertices organizados
        Coloracao coloracao;               // coloração do grafo organizado
        std::vector<int32_t> novoId;       // tabela de traducao: novoId[v original] = id no grafo organizado
    };

    // colore o grafo
    Coloracao colorir(const grafo::Grafo &grafo);

    // renomeia os vertices para que cada cor ocupe ids consecutivos 
    GrafoColorido organizarPorCor(const grafo::Grafo &grafo, const Coloracao &coloracao);

    // confere a coloração: toda aresta liga cores diferentes, as cores estão em [0, numCores) 
    std::string verificar(const grafo::Grafo &grafo, const Coloracao &coloracao);

}  // namespace coloracao
