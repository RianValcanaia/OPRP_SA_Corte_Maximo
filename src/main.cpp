/*
    main.cpp — Max-Cut com Simulated Annealing (sequencial ou com varredura colorida em OpenMP)

    Uso: ./maxcut <grafo.txt> <saida.txt> [--tempo S] [--varreduras N] [--seed N] [--t0 X] [--t1 X]
                  [--threads N] [--varredura sequencial|colorida] [--trace ARQ.csv]

    Le o grafo, roda o SA e grava a melhor particao encontrada: uma linha com n
    caracteres 0/1. Informacoes de diagnostico vao para a tela.
 */
#include <iostream>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "grafo.hpp"
#include "mpi_util.hpp"
#include "sa.hpp"

namespace {
    using Relogio = std::chrono::steady_clock;

    // tempo alvo do programa inteiro
    constexpr double TEMPO_PADRAO = 80.0;
    
    // tempo reservado para o que vem depois do SA
    constexpr double RESERVA_FINAL = 2.0;

    // de quantos em quantos segundos a melhor solucao ate agora eh gravada na saida (0 = so no fim)
    constexpr double INTERVALO_PONTO_PADRAO = 5.0;

    // maior --tempo aceito
    constexpr double TEMPO_MAXIMO = 86400.0;

    // maior --threads aceito 
    constexpr long long MAX_THREADS = 100;

    double segundosDesde(Relogio::time_point inicio) {
        return std::chrono::duration<double>(Relogio::now() - inicio).count();
    }

    void mostrarUso(const char *programa) {
        std::fprintf(stderr,
                     "uso: %s <grafo.txt> <saida.txt> [--tempo S] [--varreduras N] [--seed N] [--t0 X] [--t1 X] [--trace ARQ.csv]\n"
                     "  --tempo S       duração alvo do programa inteiro, em segundos (padrão %.0f)\n"
                     "  --varreduras N  roda N varreduras fixas em vez de usar o tempo\n"
                     "  --threads N     threads OpenMP por rank (padrão e 0 = as CPUs do rank; acima disso, reduz; 1 = sequencial)\n"
                     "  --varredura V   sequencial ou colorida\n"
                     "  --trace ARQ     grava a trajetória do SA em CSV\n"
                     "  --ponto S       grava a melhor solução na saída a cada S segundos (padrão %.0f; 0 = só no fim)\n",
                     programa, TEMPO_PADRAO, INTERVALO_PONTO_PADRAO);
    }

    uint64_t sementeAleatoria() {
        std::random_device rd;
        return (uint64_t{rd()} << 32) ^ rd() ^ static_cast<uint64_t>(Relogio::now().time_since_epoch().count());
    }

    // converte o valor de uma opcao com mensagem de erro clara (em vez de "stod"/"stoull")
    double lerReal(const std::string &chave, const std::string &valor) {
        size_t usados = 0;
        double x = 0;
        try {
            x = std::stod(valor, &usados);
        } catch (const std::exception &) {
            usados = 0;
        }
        if (usados == 0 || usados != valor.size())
            throw std::invalid_argument("valor inválido para " + chave + ": '" + valor + "' (esperado um número)");
        return x;
    }

    long long lerInteiro(const std::string &chave, const std::string &valor) {
        size_t usados = 0;
        long long x = 0;
        try {
            x = std::stoll(valor, &usados);
        } catch (const std::exception &) {
            usados = 0;
        }
        if (usados == 0 || usados != valor.size())
            throw std::invalid_argument("valor inválido para " + chave + ": '" + valor + "' (esperado um inteiro)");
        return x;
    }

    // a semente eh um inteiro sem sinal: so digitos
    uint64_t lerSemente(const std::string &chave, const std::string &valor) {
        if (valor.empty() || valor.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("valor inválido para " + chave + ": '" + valor + "' (esperado um inteiro >= 0)");
        try {
            return std::stoull(valor);
        } catch (const std::exception &) {
            throw std::invalid_argument("valor inválido para " + chave + ": '" + valor + "' (grande demais)");
        }
    }

    // Le as opcoes "--chave valor" a partir de argv[3]
    sa::Parametros lerParametros(
        int argc,
        char **argv,
        Relogio::time_point inicio,
        std::string &caminhoTraco,
        int cpusPorRank,
        bool avisar
    ) {
        sa::Parametros param;
        param.seed = sementeAleatoria();

        param.intervaloPonto = INTERVALO_PONTO_PADRAO;

        bool temTempo = false, temVarreduras = false;
        double tempoTotal = TEMPO_PADRAO;
        long long threads = 0;          // 0 = automatico (as CPUs deste rank)
        bool temThreads = false;        // o usuario escreveu --threads?
        std::string tipoVarredura;      // "", "sequencial" ou "colorida"

        for (int i = 3; i < argc; i += 2) {
            const std::string chave = argv[i];

            if (i + 1 >= argc) throw std::invalid_argument("faltou o valor de '" + chave + "'");
            const std::string valor = argv[i + 1];

            if (chave == "--seed") 
                param.seed = lerSemente(chave, valor);
            else if (chave == "--t0") 
                param.T0 = lerReal(chave, valor);
            else if (chave == "--t1") 
                param.T1 = lerReal(chave, valor);
            else if (chave == "--tempo") { 
                tempoTotal = lerReal(chave, valor); 
                temTempo = true; 
            }
            else if (chave == "--varreduras") { 
                param.varreduras = static_cast<long>(lerInteiro(chave, valor)); 
                temVarreduras = true; 
            }
            else if (chave == "--trace") 
                caminhoTraco = valor;
            else if (chave == "--threads") {
                threads = lerInteiro(chave, valor);
                temThreads = true;
            }
            else if (chave == "--varredura")
                tipoVarredura = valor;
            else if (chave == "--ponto") {
                param.intervaloPonto = lerReal(chave, valor);
                if (param.intervaloPonto < 0) throw std::invalid_argument("--ponto deve ser >= 0 (0 desliga)");
            }
            else throw std::invalid_argument("argumento desconhecido '" + chave + "'");
        }

        if (temTempo && temVarreduras) 
            throw std::invalid_argument("--tempo e --varreduras não podem ser usados juntos");
        
        if (!(tempoTotal > 0 && tempoTotal <= TEMPO_MAXIMO))
            throw std::invalid_argument("--tempo deve estar entre 0 (exclusive) e " + std::to_string(static_cast<long>(TEMPO_MAXIMO)) + " segundos");

        if (threads < 0 || threads > MAX_THREADS)
            throw std::invalid_argument("--threads deve estar entre 0 (automatico) e " + std::to_string(MAX_THREADS));

        // Mais threads que CPUs piora muito: uma thread sem CPU faz as outras esperarem na barreira de cada cor
        // (medido 4 a 5 vezes mais lento com 12 threads em 8 CPUs). 0 = todas as CPUs deste rank.
        const long long pedido = threads;
        const long long disponiveis = std::max(1, cpusPorRank);
        if (threads == 0)
            threads = disponiveis;
        else if (threads > disponiveis) {
            if (avisar)
                std::fprintf(stderr, "nota: --threads %lld excede as %lld CPUs disponiveis por rank; usando %lld\n", threads, disponiveis, disponiveis);
            threads = disponiveis;
        }

        // sem --varredura: colorida (OpenMP) se houver mais de 1 thread, senao sequencial
        if (tipoVarredura.empty())
            param.colorida = threads > 1;
        else if (tipoVarredura == "colorida")
            param.colorida = true;
        else if (tipoVarredura == "sequencial") {
            // pedir threads (diferente de 1) junto com a varredura sequencial nao faz sentido
            if (temThreads && pedido != 1)
                throw std::invalid_argument("--varredura sequencial nao combina com --threads maior que 1");
            param.colorida = false;
            threads = 1;
        } else
            throw std::invalid_argument("valor inválido para --varredura: '" + tipoVarredura + "' (use sequencial ou colorida)");
        param.threads = static_cast<int>(threads);

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

        // primeiro num arquivo temporario ao lado, depois rename se o processo for morto no meio da
        // gravacao, a saida anterior continua inteira e valida, nunca um arquivo cortado pela metade.
        const std::string temporario = caminho + ".tmp";
        FILE *t = std::fopen(temporario.c_str(), "wb");

        if (t) {
            const size_t escritos = std::fwrite(linha.data(), 1, linha.size(), t);
            const bool ok = (std::fclose(t) == 0) && escritos == linha.size();
            if (ok && std::rename(temporario.c_str(), caminho.c_str()) == 0) return;
            std::remove(temporario.c_str());
        }

        // sem permissao para criar o temporario (ou rename recusado, ex.: saida eh um dispositivo): grava direto
        FILE *f = std::fopen(caminho.c_str(), "wb");

        if (!f) throw std::runtime_error("não foi possível criar '" + caminho + "'");

        size_t escritos = std::fwrite(linha.data(), 1, linha.size(), f);

        bool ok = (std::fclose(f) == 0) && escritos == linha.size();
        if (!ok) throw std::runtime_error("falha ao escrever '" + caminho + "'");
    }

}  // namespace

namespace {
    int executarPrograma(
        int argc, 
        char **argv, 
        Relogio::time_point inicio, 
        const dist::Ambiente &amb
    ) {
        const bool rank0 = amb.rank == 0;

        if (argc < 3) {
            if (rank0) mostrarUso(argv[0]);
            return 1;
        }
        const std::string CAMINHO_GRAFO = argv[1];
        const std::string CAMINHO_SAIDA = argv[2];

        try {
            std::string caminhoTraco;
            sa::Parametros param = lerParametros(argc, argv, inicio, caminhoTraco, amb.cpusPorRank, rank0);

            // so o rank 0 grava a trajetoria
            std::ofstream arquivoTraco;
            if (!caminhoTraco.empty() && rank0) {
                arquivoTraco.open(caminhoTraco);
                if (!arquivoTraco) throw std::runtime_error("não foi possível criar '" + caminhoTraco + "'");
                param.traco = &arquivoTraco;
            }

            // o rank 0 le o arquivo e manda o grafo aos outros: os outros nos nao precisam enxergar o arquivo
            grafo::Grafo g;
            std::string erroDaLeitura;
            if (rank0) {
                try {
                    g = grafo::lerArquivo(CAMINHO_GRAFO);
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

            const double tempo_leitura = segundosDesde(inicio);

            // cada rank comecou em um instante um pouco diferente, entao todos adotam o maior
            // tempo ja gasto entre eles e terminam juntos
            if (param.guiadoPorTempo) {
                const double orcamento = std::chrono::duration<double>(param.prazo - inicio).count();
                const double gasto = dist::maximoEntreRanks(segundosDesde(inicio));
                param.prazo = Relogio::now() + std::chrono::duration_cast<Relogio::duration>(std::chrono::duration<double>(orcamento - gasto));
            }

            // sementes: a do rank 0 vale para todos; cada rank soma um deslocamento (rank 0 fica com a original)
            const uint64_t sementeBase = dist::valorDoRank0(param.seed);
            param.seed = sementeBase + static_cast<uint64_t>(amb.rank) * 0x9E3779B97F4A7C15ULL;

            if (rank0 && param.intervaloPonto > 0) {
                bool avisou = false;
                param.aoSalvarPonto = [&CAMINHO_SAIDA, &avisou](const std::vector<uint8_t> &particao, int64_t) {
                    try {
                        escreverSolucao(CAMINHO_SAIDA, particao);
                    } catch (const std::exception &erro) {
                        if (!avisou) std::fprintf(stderr, "nota: nao foi possivel gravar o ponto de controle: %s\n", erro.what());
                        avisou = true;
                    }
                };
            } else {
                param.intervaloPonto = 0;
            }

            // se a cadeia deste rank falhar, ele continua nas chamadas coletivas com corte "infinitamente ruim"
            sa::Resultado r;
            int64_t corteLocal = LLONG_MIN;
            std::string falha;
            try {
                r = sa::executar(g, param);
                corteLocal = grafo::valorDoCorte(g, r.melhor);
            } catch (const std::exception &e) {
                falha = e.what();
                std::fprintf(stderr, "rank %d: erro no SA: %s\n", amb.rank, falha.c_str());
            }

            const dist::MelhorCorte melhor = dist::melhorEntreRanks(corteLocal);
            if (melhor.corte == LLONG_MIN) {
                if (rank0) std::fprintf(stderr, "erro: nenhum rank conseguiu rodar o SA\n");
                return 1;
            }

            std::vector<uint8_t> particao = r.melhor;
            if (rank0) particao.resize(g.qtd_vertices);
            dist::enviarParticaoAoRank0(amb, melhor.rank, particao);
            const std::vector<int64_t> cortes = dist::reunirCortes(amb, corteLocal);

            if (!r.aviso.empty())
                std::fprintf(stderr, "AVISO: auto-verificação falhou (rank %d): %s\n", amb.rank, r.aviso.c_str());

            if (!rank0) return 0;

            // a saida eh gravada mesmo que a auto-verificacao tenha acusado divergencia
            escreverSolucao(CAMINHO_SAIDA, particao);

            std::cout << "n=" << g.qtd_vertices << " m=" << g.qtd_arestas << " grau_max=" << g.grauMaximo() << '\n'
                      << "T0=" << param.T0 << " T1=" << param.T1 << " seed=" << sementeBase << '\n'
                      << "modo=" << (param.guiadoPorTempo ? "tempo" : "varreduras fixas") << " varredura="
                      << (param.colorida ? "colorida (" + std::to_string(r.numCores) + " cores, " + std::to_string(param.threads) + " threads)"
                                         : std::string("sequencial")) << '\n'
                      << "mpi: ranks=" << amb.tamanho << " (ranks neste no=" << amb.tamanhoLocal << ", CPUs por rank=" << amb.cpusPorRank
                      << ") melhor=rank " << melhor.rank << '\n';
            if (amb.tamanho > 1) {
                std::cout << "corte de cada rank:";
                for (int64_t c : cortes) std::cout << ' ' << c;
                std::cout << '\n';
            }
            std::cout << "tempo: leitura=" << tempo_leitura << "s sa=" << r.segundos << "s total=" << segundosDesde(inicio)
                      << "s | varreduras=" << r.varreduras << " ("
                      << (r.segundos > 0 ? static_cast<double>(r.varreduras) / r.segundos : 0.0) << "/s) (rank 0)\n"
                      << "corte: inicial=" << r.corteInicial << " final=" << r.corteFinal << " melhor=" << r.corteMelhor
                      << " (varredura " << r.varreduraDaMelhor << ") | apos descida gulosa=" << r.corteDescida
                      << " | escolhido=" << melhor.corte << '\n';
        } catch (const std::invalid_argument &erro) {
            // argumentos invalidos: todos os ranks veem o mesmo erro e saem juntos
            if (rank0) {
                std::fprintf(stderr, "erro: %s\n", erro.what());
                mostrarUso(argv[0]);
            }
            return 1;
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
