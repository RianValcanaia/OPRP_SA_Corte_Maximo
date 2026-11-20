/*
    main.cpp — Max-Cut com Simulated Annealing (versão sequencial)

    Uso: ./maxcut <grafo.txt> <saida.txt> [--tempo S] [--varreduras N] [--seed N] [--t0 X] [--t1 X]

    Lê o grafo, roda o SA e grava a melhor partição encontrada: uma linha com n
    caracteres 0/1. Informações de diagnóstico vão para a tela.
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
#include "sa.hpp"

namespace {
    using Relogio = std::chrono::steady_clock;

    // tempo alvo do programa inteiro
    constexpr double TEMPO_PADRAO = 80.0;
    
    // tempo reservado para o que vem depois do SA
    constexpr double RESERVA_FINAL = 2.0;

    double segundosDesde(Relogio::time_point inicio) {
        return std::chrono::duration<double>(Relogio::now() - inicio).count();
    }

    void mostrarUso(const char *programa) {
        std::fprintf(stderr,
                     "uso: %s <grafo.txt> <saida.txt> [--tempo S] [--varreduras N] [--seed N] [--t0 X] [--t1 X]\n"
                     "  --tempo S       duração alvo do programa inteiro, em segundos (padrão %.0f)\n"
                     "  --varreduras N  roda N varreduras fixas em vez de usar o tempo\n",
                     programa, TEMPO_PADRAO);
    }

    uint64_t sementeAleatoria() {
        std::random_device rd;
        return (uint64_t{rd()} << 32) ^ rd() ^ static_cast<uint64_t>(Relogio::now().time_since_epoch().count());
    }

    // Le as opcoes "--chave valor" a partir de argv[3]
    sa::Parametros lerParametros(
        int argc, 
        char **argv, 
        Relogio::time_point inicio
    ) {
        sa::Parametros param;
        param.seed = sementeAleatoria();

        bool temTempo = false, temVarreduras = false;
        double tempoTotal = TEMPO_PADRAO;

        for (int i = 3; i < argc; i += 2) {
            const std::string chave = argv[i];

            if (i + 1 >= argc) throw std::invalid_argument("faltou o valor de '" + chave + "'");
            const std::string valor = argv[i + 1];

            if (chave == "--seed") param.seed = std::stoull(valor);
            else if (chave == "--t0") param.T0 = std::stod(valor);
            else if (chave == "--t1") param.T1 = std::stod(valor);
            else if (chave == "--tempo") { tempoTotal = std::stod(valor); temTempo = true; }
            else if (chave == "--varreduras") { param.varreduras = std::stol(valor); temVarreduras = true; }
            else throw std::invalid_argument("argumento desconhecido '" + chave + "'");
        }

        if (temTempo && temVarreduras) 
            throw std::invalid_argument("--tempo e --varreduras não podem ser usados juntos");
        
        if (!(tempoTotal > 0)) throw std::invalid_argument("--tempo deve ser > 0");

        param.guiadoPorTempo = !temVarreduras;
        if (param.guiadoPorTempo) {
            param.prazo = inicio + std::chrono::duration_cast<Relogio::duration>(std::chrono::duration<double>(tempoTotal - RESERVA_FINAL));
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
        const sa::Parametros param = lerParametros(argc, argv, inicio);

        const grafo::Grafo g = grafo::lerArquivo(CAMINHO_GRAFO);
        const double tempo_leitura = segundosDesde(inicio);

        const sa::Resultado r = sa::executar(g, param);

        // a saida eh gravada mesmo que a auto-verificacao tenha acusado divergencia
        escreverSolucao(CAMINHO_SAIDA, r.melhor);

        std::cout << "n=" << g.qtd_vertices << " m=" << g.qtd_arestas << " grau_max=" << g.grauMaximo() << '\n'
                  << "T0=" << param.T0 << " T1=" << param.T1 << " seed=" << param.seed << '\n' 
                  << "modo=" << (param.guiadoPorTempo ? "tempo" : "varreduras fixas") << '\n'
                  << "tempo: leitura=" << tempo_leitura << "s sa=" << r.segundos << "s total=" << segundosDesde(inicio)
                  << "s | varreduras=" << r.varreduras << " ("
                  << (r.segundos > 0 ? static_cast<double>(r.varreduras) / r.segundos : 0.0) << "/s)\n"
                  << "corte: inicial=" << r.corteInicial << " final=" << r.corteFinal << " melhor=" << r.corteMelhor
                  << " (varredura " << r.varreduraDaMelhor << ")\n";

        if (!r.aviso.empty()) 
            std::fprintf(stderr, "AVISO: auto-verificação falhou: %s\n", r.aviso.c_str());

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
