/*
    main.cpp — esqueleto do TF de Max-Cut

    Uso: ./maxcut <grafo.txt> <saida.txt> [--seed N]
 */
#include <iostream>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "grafo.hpp"

namespace {

    using Relogio = std::chrono::steady_clock;

    double segundosDesde(Relogio::time_point inicio) {
        return std::chrono::duration<double>(Relogio::now() - inicio).count();
    }

    uint64_t proximoSplitmix(uint64_t &estado) {
        uint64_t z = (estado += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    void mostrarUso(const char *programa) {
        std::fprintf(stderr, "uso: %s <grafo.txt> <saida.txt> [--seed N]\n", programa);
    }


    void escreverSolucao(
        const std::string &caminho, 
        const std::vector<uint8_t> &x
    ){
        std::string linha(x.size() + 1, '0');
        
        for (size_t i = 0; i < x.size(); ++i) linha[i] = x[i] ? '1' : '0';
        linha[x.size()] = '\n';

        FILE *f = std::fopen(caminho.c_str(), "wb");

        if (!f) throw std::runtime_error("não foi possível criar '" + caminho + "'");

        size_t escritos = std::fwrite(linha.data(), 1, linha.size(), f);
        
        bool ok = (std::fclose(f) == 0) && escritos == linha.size();
        if (!ok) throw std::runtime_error("falha ao escrever '" + caminho + "'");
    }

}  // namespace

int main(int argc, char **argv) {
    const auto inicio = Relogio::now();

    if (argc < 3) {
        mostrarUso(argv[0]);
        return 1;
    }
    const std::string CAMINHO_GRAFO = argv[1];
    const std::string CAMINHO_SAIDA = argv[2];

    // semente fixa por enquanto para depurar
    uint64_t seed = 12345;
    
    // for (int i = 3; i < argc; i += 2) {
    //     std::string chave = argv[i];
    //     if (chave == "--seed" && i + 1 < argc) {
    //         seed = std::strtoull(argv[i + 1], nullptr, 10);
    //     } else {
    //         std::fprintf(stderr, "erro: argumento desconhecido ou sem valor: '%s'\n", argv[i]);
    //         mostrarUso(argv[0]);
    //         return 1;
    //     }
    // }

    try {
        grafo::Grafo grafo = grafo::lerArquivo(CAMINHO_GRAFO);
        const double tempo_leitura = segundosDesde(inicio);

        // Partição aleatória (placeholder do SA).
        std::vector<uint8_t> particao(grafo.qtd_vertices);
        uint64_t estado = seed;

        for (int32_t v = 0; v < grafo.qtd_vertices; ++v) 
            particao[v] = static_cast<uint8_t>(proximoSplitmix(estado) & 1u);

        escreverSolucao(CAMINHO_SAIDA, particao);

        std::cout << "n=" << grafo.qtd_vertices << " m=" << grafo.qtd_arestas << " grau_max= " << grafo.grauMaximo() << std::endl
                  << "tempo de leitura=" << tempo_leitura << " tempo total=" << segundosDesde(inicio) << std::endl 
                  << "corte aleatorio=" << static_cast<long long>(grafo::valorDoCorte(grafo, particao)) 
                  << "seed=" << seed << std::endl;

    } catch (const std::exception &erro) {
        std::fprintf(stderr, "erro: %s\n", erro.what());
        return 1;
    }
    return 0;
}
