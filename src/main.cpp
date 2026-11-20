/*
    main.cpp — Max-Cut com Simulated Annealing (versão sequencial)

    Uso: ./maxcut <grafo.txt> <saida.txt> [--seed N] [--t0 X] [--t1 X] [--varreduras S]

    Lê o grafo, roda o SA e grava a melhor partição encontrada: uma linha com n
    caracteres 0/1. Informações de diagnóstico vão para a tela.
 */
#include <iostream>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "grafo.hpp"
#include "sa.hpp"

namespace {

    using Relogio = std::chrono::steady_clock;

    double segundosDesde(Relogio::time_point inicio) {
        return std::chrono::duration<double>(Relogio::now() - inicio).count();
    }

    void mostrarUso(const char *programa) {
        std::fprintf(stderr, "uso: %s <grafo.txt> <saida.txt> [--seed N] [--t0 X] [--t1 X] [--varreduras S]\n", programa);
    }

    // Le as opcoes "--chave valor" a partir de argv[3].
    sa::Parametros lerParametros(int argc, char **argv) {
        sa::Parametros param;

        for (int i = 3; i < argc; i += 2) {
            const std::string chave = argv[i];
            
            if (i + 1 >= argc) throw std::invalid_argument("faltou o valor de '" + chave + "'");
            const std::string valor = argv[i + 1];

            if (chave == "--seed") param.seed = std::stoull(valor);
            else if (chave == "--t0") param.T0 = std::stod(valor);
            else if (chave == "--t1") param.T1 = std::stod(valor);
            else if (chave == "--varreduras") param.varreduras = std::stol(valor);
            else throw std::invalid_argument("argumento desconhecido '" + chave + "'");
        }
        return param;
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

    try {
        const sa::Parametros param = lerParametros(argc, argv);

        const grafo::Grafo g = grafo::lerArquivo(CAMINHO_GRAFO);
        const double tempo_leitura = segundosDesde(inicio);

        const sa::Resultado r = sa::executar(g, param);

        escreverSolucao(CAMINHO_SAIDA, r.melhor);

        std::cout << "n=" << g.qtd_vertices << " m=" << g.qtd_arestas << " grau_max=" << g.grauMaximo() << '\n'
                  << "T0=" << param.T0 << " T1=" << param.T1 << " varreduras=" << param.varreduras
                  << " seed=" << param.seed << '\n'
                  << "tempo: leitura=" << tempo_leitura << "s sa=" << r.segundos << "s ("
                  << (r.segundos > 0 ? static_cast<double>(param.varreduras) / r.segundos : 0.0) << " varreduras/s)"
                  << " total=" << segundosDesde(inicio) << "s\n"
                  << "corte: inicial=" << r.corteInicial << " final=" << r.corteFinal << " melhor=" << r.corteMelhor
                  << " (varredura " << r.varreduraDaMelhor << ")\n";

    } catch (const std::invalid_argument &erro) {
        std::fprintf(stderr, "erro: %s\n", erro.what());
        mostrarUso(argv[0]);
        return 1;
    } catch (const std::exception &erro) {
        std::fprintf(stderr, "erro: %s\n", erro.what());
        return 1;
    }
    return 0;
}
