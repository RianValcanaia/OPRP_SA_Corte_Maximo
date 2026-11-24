/*
    competicao.cpp — binario da competicao (Max-Cut com Simulated Annealing, MPI + OpenMP)

    Uso: mpirun <opcoes do mpi> ./competicao <grafo.txt> <saida.txt>
 */
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "grafo.hpp"
#include "mpi_util.hpp"
#include "sa.hpp"

namespace {
    using Relogio = std::chrono::steady_clock;

    // janela de tempo do programa inteiro, contada a partir do inicio do processo
    constexpr double TEMPO_TOTAL = 90.0;

    // sobra para o que vem depois do SA: descida gulosa, escolher a melhor entre os ranks, gravar e encerrar o MPI
    constexpr double RESERVA_FINAL = 2.0;

    // de quantos em quantos segundos a melhor solucao eh gravada na saida
    constexpr double INTERVALO_PONTO = 5.0;

    struct Temperaturas {
        double t0;
        double t1;
    };

    Temperaturas escolherTemperaturas(int32_t qtdVertices) {
        if (qtdVertices <= 5000) return {2.0, 0.15};        // treino
        if (qtdVertices <= 50000) return {2.0, 0.15};       // media
        return {2.0, 0.15};                                 // grande
    }

    double segundosDesde(Relogio::time_point inicio) {
        return std::chrono::duration<double>(Relogio::now() - inicio).count();
    }

    uint64_t sementeAleatoria() {
        std::random_device rd;
        return (uint64_t{rd()} << 32) ^ rd() ^ static_cast<uint64_t>(Relogio::now().time_since_epoch().count());
    }

    void escreverSolucao(
        const std::string &caminho, 
        const std::vector<uint8_t> &particao
    ) {

        // monta a linha de texto
        std::string linha(particao.size() + 1, '0');
        for (size_t i = 0; i < particao.size(); ++i) linha[i] = particao[i] ? '1' : '0';
        linha[particao.size()] = '\n';

        // escreve em um arquivo temporario 
        const std::string temporario = caminho + ".tmp";
        FILE *t = std::fopen(temporario.c_str(), "wb");
        if (t) {
            const size_t escritos = std::fwrite(linha.data(), 1, linha.size(), t);
            const bool ok = (std::fclose(t) == 0) && escritos == linha.size();
            if (ok && std::rename(temporario.c_str(), caminho.c_str()) == 0) return;
            std::remove(temporario.c_str());
        }

        FILE *f = std::fopen(caminho.c_str(), "wb");
        if (!f) throw std::runtime_error("nao foi possivel criar '" + caminho + "'");

        // confere quantos foram escritos e retorna erro 
        const size_t escritos = std::fwrite(linha.data(), 1, linha.size(), f);
        const bool ok = (std::fclose(f) == 0) && escritos == linha.size();

        if (!ok) throw std::runtime_error("falha ao escrever '" + caminho + "'");
    }

    // Todos os ranks passam pelas mesmas chamadas coletivas, na mesma ordem. 
    int executarPrograma(
        int argc, char **argv, 
        Relogio::time_point inicio, 
        const dist::Ambiente &amb
    ) {
        const bool rank0 = amb.rank == 0;

        if (argc != 3) {
            if (rank0) 
                std::fprintf(stderr, "uso: mpirun <opcoes do mpi> %s <grafo.txt> <saida.txt>\n", argv[0]);
            return 1;
        }

        const std::string caminhoGrafo = argv[1];
        const std::string caminhoSaida = argv[2];

        try {
            // so o rank 0 le o arquivo e manda o grafo aos outros
            grafo::Grafo g;
            std::string erroDaLeitura;

            if (rank0) {
                try {
                    g = grafo::lerArquivo(caminhoGrafo);
                } catch (const std::exception &erro) {
                    erroDaLeitura = erro.what();
                    if (erroDaLeitura.empty()) erroDaLeitura = "falha ao ler o grafo";
                }
            }
            
            const std::string erro = dist::difundirGrafo(amb, g, erroDaLeitura);
            if (!erro.empty()) {
                if (rank0) std::fprintf(stderr, "erro: %s\n", erro.c_str());
                return 1;
            }
            const double tempoLeitura = segundosDesde(inicio);

            // parametros do SA
            sa::Parametros param;
            const Temperaturas temp = escolherTemperaturas(g.qtd_vertices);
            param.T0 = temp.t0;
            param.T1 = temp.t1;
            param.guiadoPorTempo = true;
            param.threads = std::max(1, amb.nucleosPorRank);     // uma thread por nucleo fisico
            param.colorida = param.threads > 1;

            // prazo comum
            const double gasto = dist::maximoEntreRanks(segundosDesde(inicio));
            param.prazo = Relogio::now() + std::chrono::duration_cast<Relogio::duration>(
                std::chrono::duration<double>(TEMPO_TOTAL - RESERVA_FINAL - gasto));

            // semente do rank 0 vale para todos, cada rank soma um deslocamento
            const uint64_t sementeBase = dist::valorDoRank0(sementeAleatoria());
            param.seed = sementeBase + static_cast<uint64_t>(amb.rank) * 0x9E3779B97F4A7C15ULL;

            // pontos de controle: so o rank 0 grava, a melhor solucao da sua propria cadeia (nao ha comunicacao
            // no meio do SA). A gravacao final escolhe a melhor entre todos os ranks.
            if (rank0) {
                bool avisou = false;
                param.intervaloPonto = INTERVALO_PONTO;
                param.aoSalvarPonto = [&caminhoSaida, &avisou](const std::vector<uint8_t> &particao, int64_t) {
                    try {
                        escreverSolucao(caminhoSaida, particao);
                    } catch (const std::exception &e) {
                        if (!avisou) std::fprintf(stderr, "nao foi possivel gravar o ponto de controle: %s\n", e.what());
                        avisou = true;
                    }
                };
            }

            // cria um resultado vazio
            sa::Resultado r;
            int64_t corteLocal = LLONG_MIN;
            
            try {
                r = sa::executar(g, param);
                corteLocal = grafo::valorDoCorte(g, r.melhor);
            } catch (const std::exception &e) {
                std::fprintf(stderr, "rank %d: erro no SA: %s\n", amb.rank, e.what());
            }

            const dist::MelhorCorte melhor = dist::melhorEntreRanks(corteLocal);
            if (melhor.corte == LLONG_MIN) {
                if (rank0) std::fprintf(stderr, "erro: nenhum rank conseguiu rodar o SA\n");
                return 1;
            }

            std::vector<uint8_t> particao = r.melhor;
            if (rank0) particao.resize(g.qtd_vertices);
            dist::enviarParticaoAoRank0(amb, melhor.rank, particao);

            if (!r.aviso.empty())
                std::fprintf(stderr, "AVISO: auto-verificacao falhou (rank %d): %s\n", amb.rank, r.aviso.c_str());

            if (!rank0) return 0;

            escreverSolucao(caminhoSaida, particao);

            std::cout << "n=" << g.qtd_vertices << " m=" << g.qtd_arestas << " T0=" << temp.t0 << " T1=" << temp.t1
                      << " ranks=" << amb.tamanho << " threads=" << param.threads << '\n'
                      << "tempo: leitura=" << tempoLeitura << "s total=" << segundosDesde(inicio) << "s | corte=" << melhor.corte
                      << " (rank " << melhor.rank << ")\n";
        } catch (const std::exception &erro) {
            std::fprintf(stderr, "erro: %s\n", erro.what());
            return 1;
        }
        return 0;
    }

}  // namespace

int main(int argc, char **argv) {
    const auto inicio = Relogio::now();

    dist::Ambiente amb = dist::iniciar(argc, argv);
    const int codigo = executarPrograma(argc, argv, inicio, amb);
    dist::finalizar();
    return codigo;
}
