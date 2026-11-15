/*
    grafo.hpp — grafo do Max-Cut em formato CSR
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace grafo {

struct Grafo {
    int32_t qtd_vertices = 0;              // numero de vertices
    int64_t qtd_arestas = 0;              // numero de arestas
    std::vector<int32_t> offsets;    // onnde comeca o vertice em viz
    std::vector<int32_t> vizinhos;       // 2 * (arestas sem laço) posições
    std::vector<int8_t> peso;      // +1 ou -1, alinhado com viz

    int32_t grau(int32_t v) const { return offsets[v + 1] - offsets[v]; }
    int32_t grauMaximo() const;
};

// Lê o arquivo inteiro de uma vez e monta o CSR.
// Lança std::runtime_error com a descrição do problema se o arquivo for inválido.
Grafo lerArquivo(const std::string &caminho);

// Valor do corte da partição x (x[v] em {0,1}), calculado do zero em O(m).
// Usado para conferir resultados; o valor oficial vem do verificador externo.
int64_t valorDoCorte(const Grafo &g, const std::vector<uint8_t> &x);

}
