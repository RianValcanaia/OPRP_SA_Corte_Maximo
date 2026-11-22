#include "sa.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>
#include <string>

#include "coloracao.hpp"
#include "random.hpp"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace sa {

    namespace {
        // quantos vertices visitar, no minimo, entre duas consultas ao relogio, para nao 
        // sobrecarregar com consultas.
        constexpr long VISITAS_ENTRE_CONSULTAS = 20000;

        // Bit que marca se é negativo (0 = +1; 1 = -1)
        constexpr uint32_t BIT_NEGATIVO = 0x80000000u;
        
        // estado do SA: particao, ganho de cada vertice e valor do corte
        struct Estado {
            std::vector<uint8_t> particao;
            std::vector<int32_t> delta;
            int64_t corte = 0;
        };

        // ganho de trocar o vertice de lado
        int32_t calcularDelta(
            const grafo::Grafo &grafo,
            const std::vector<uint8_t> &particao,
            int32_t vertice     // vertice cujo ganho sera calculado
        ) {
            int32_t soma = 0;
            for (int32_t k = grafo.offsets[vertice]; k < grafo.offsets[vertice + 1]; ++k){
                const int32_t vizinho = grafo.vizinhos[k];

                if (particao[vizinho] == particao[vertice])
                    soma += grafo.peso[k];      // mesmo lado
                else 
                    soma -= grafo.peso[k];      // lado diferente
            }

            return soma;
        }

        // estado completo de uma particao dada: delta de todos os vertices + corte, tudo calculado do zero.
        Estado estadoDaParticao(
            const grafo::Grafo &grafo,
            std::vector<uint8_t> particao
        ) {
            Estado e;
            e.particao = std::move(particao);

            // calcula o vetor delta da particao
            e.delta.resize(grafo.qtd_vertices);
            for (int32_t v = 0; v < grafo.qtd_vertices; ++v)
                e.delta[v] = calcularDelta(grafo, e.particao, v);

            e.corte = grafo::valorDoCorte(grafo, e.particao);

            return e;
        }

        // particao aleatoria + o estado completo dela.
        Estado estadoInicial(
            const grafo::Grafo &grafo,
            rng::Gerador &gerador
        ) {
            std::vector<uint8_t> particao(grafo.qtd_vertices);

            // sorteia o lado de cada vertice
            for (auto &lado : particao)
                lado = static_cast<uint8_t>(gerador() & 1u);

            return estadoDaParticao(grafo, std::move(particao));
        }

        // adjacencia compactada 32 bits: 1 bit de sinal (1 = -1; 0 = +1) + 31 bits para os vizinhos
        // assim o laco faz uma leitura em vez de duas (vizinhos[] e peso[]).
        std::vector<uint32_t> compactarAdjacencia(
            const grafo::Grafo &grafo
        ) {
            std::vector<uint32_t> adjacencia(grafo.vizinhos.size());

            for (size_t k = 0; k < adjacencia.size(); ++k)
                adjacencia[k] = static_cast<uint32_t>(grafo.vizinhos[k]) | (grafo.peso[k] < 0 ? BIT_NEGATIVO : 0u);

            return adjacencia;
        }

        // tabela de limiares de aceitacao para a temperatura T
        // delta esta em [-grau_max, +grau_max]
        // limiares[0] aceita sempre (delta >= 0). 
        // limiares[m] = P(aceitar uma piora de m) * 2^32
        void montarLimiares(
            std::vector<uint32_t> &limiares,
            double temp
        ) {
            limiares[0] = UINT32_MAX;   // simula infi

            for (size_t m = 1; m < limiares.size(); ++m) {
                const double p = std::exp(-static_cast<double>(m) / temp);
                limiares[m] = static_cast<uint32_t>(std::min(p * 4294967296.0, 4294967295.0));
            }
        }

        // uma varredura (linhas 4 a 9)
        void varrer(
            const grafo::Grafo &grafo,
            const std::vector<uint32_t> &adjacencia,
            const std::vector<uint32_t> &limiares,
            Estado &estado,
            rng::Gerador &gerador
        ) {
            const int32_t qtd_vertices = grafo.qtd_vertices;
            const int32_t *offsets = grafo.offsets.data();
            const uint32_t *adj = adjacencia.data();
            const uint32_t *limiar = limiares.data();

            uint8_t *particao = estado.particao.data();
            int32_t *delta = estado.delta.data();

            for (int32_t i = 0; i < qtd_vertices; ++i) {
                const int32_t d = delta[i];

                const uint32_t sorteio = static_cast<uint32_t>(gerador() >> 32);
                
                uint32_t lim = UINT32_MAX;      // d >= 0 aceita sempre
                if(d < 0) lim = limiar[-d];     
                if(sorteio > lim) continue;

                // aplica o movimento
                estado.corte += d;

                // inverte o bit
                particao[i] = !particao[i];

                // troca de sinal
                delta[i] = -d;

                // atualiza o delta dos vizinhos de i
                const uint32_t particao_i = particao[i];
                for (int32_t k = offsets[i]; k < offsets[i + 1]; ++k) {
                    const uint32_t entrada = adj[k];       // le a palavra do vizinho
                    const uint32_t j = entrada & ~BIT_NEGATIVO;     // extrai o numero do vizinho 
                    
                    // flag = (peso negativo?) ^ (lados diferentes?)
                    //                                       peso negativo      lado de j     lado de i
                    delta[j] += 2 - 4 * static_cast<int32_t>((entrada >> 31) ^ particao[j] ^ particao_i); 
                    
                    //   peso | lados      | peso negativo? | lados diferentes? | flag | delta[j] muda
                    //   +1   | iguais     |       0        |         0         |  0   |     +2
                    //   +1   | diferentes |       0        |         1         |  1   |     -2
                    //   -1   | iguais     |       1        |         0         |  1   |     -2
                    //   -1   | diferentes |       1        |         1         |  0   |     +2
                }
            }
        }

        // embaralha (base, i), cada vertice tem o seu sorteio, sem estado compartilhado
        // entre threads, e o resultado nao depende de quantas threads dividem o trabalho.
        inline uint32_t sorteioDoVertice(
            uint64_t base, 
            int32_t i
        ) {
            uint64_t z = base + static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return static_cast<uint32_t>((z ^ (z >> 31)) >> 32);
        }

        // o grafo ja vem organizado por cor, percorre as cores uma depois da outra
        void varrerColorido(
            const grafo::Grafo &grafo,
            const std::vector<uint32_t> &adjacencia,
            const coloracao::Coloracao &cores,
            const std::vector<uint32_t> &limiares,
            Estado &estado,
            uint64_t base,
            int threads
        ) {
            const int32_t *offsets = grafo.offsets.data();
            const uint32_t *adj = adjacencia.data();
            const uint32_t *limiar = limiares.data();
            const int32_t *inicio = cores.inicio.data();   // o grafo esta organizado por cor

            uint8_t *particao = estado.particao.data();
            int32_t *delta = estado.delta.data();

            int64_t ganho = 0;      // soma dos ganhos dos movimentos aceitos

            #pragma omp parallel num_threads(threads)
            {
                for (int32_t c = 0; c < cores.numCores; ++c) {
                    #pragma omp for schedule(static) reduction(+ : ganho)
                    for (int32_t i = inicio[c]; i < inicio[c + 1]; ++i) {
                        const int32_t d = delta[i];

                        const uint32_t sorteio = sorteioDoVertice(base, i);
                        uint32_t lim = UINT32_MAX;
                        if (d < 0) lim = limiar[-d];
                        if (sorteio > lim) continue;

                        ganho += d;
                        particao[i] = !particao[i];
                        delta[i] = -d;

                        const uint32_t particao_i = particao[i];
                        for (int32_t k = offsets[i]; k < offsets[i + 1]; ++k) {
                            const uint32_t entrada = adj[k];
                            const uint32_t j = entrada & ~BIT_NEGATIVO;
                            const int32_t variacao = 2 - 4 * static_cast<int32_t>((entrada >> 31) ^ particao[j] ^ particao_i);

                            #pragma omp atomic
                            delta[j] += variacao;
                        }
                    }
                }
            }

            estado.corte += ganho;
        }

        // aceita so movimentos de ganho estritamente positivo e varre
        // ate nenhum vertice poder melhorar o corte 
        long descidaGulosa(
            const grafo::Grafo &grafo,
            const std::vector<uint32_t> &adjacencia,
            Estado &estado
        ) {
            const int32_t qtd_vertices = grafo.qtd_vertices;
            const int32_t *offsets = grafo.offsets.data();
            const uint32_t *adj = adjacencia.data();

            uint8_t *particao = estado.particao.data();
            int32_t *delta = estado.delta.data();

            long varreduras = 0;
            bool melhorou = true;

            while (melhorou) {
                melhorou = false;
                ++varreduras;

                for (int32_t i = 0; i < qtd_vertices; ++i) {
                    const int32_t d = delta[i];
                    if (d <= 0) continue;       // so aceita ganho estritamente positivo
                    melhorou = true;

                    // aplica o movimento
                    estado.corte += d;
                    particao[i] = !particao[i];
                    delta[i] = -d;

                    const uint32_t particao_i = particao[i];
                    for (int32_t k = offsets[i]; k < offsets[i + 1]; ++k) {
                        const uint32_t entrada = adj[k];
                        const uint32_t j = entrada & ~BIT_NEGATIVO;
                        delta[j] += 2 - 4 * static_cast<int32_t>((entrada >> 31) ^ particao[j] ^ particao_i);
                    }
                }
            }

            return varreduras;
        }

        // verificacao de seguranca: confere o estado, recalcula do zero o corte e todos os deltas e compara com o atual
        std::string verificarEstado(
            const grafo::Grafo &grafo,
            const Estado &estado
        ) {
            const int64_t esperado = grafo::valorDoCorte(grafo, estado.particao);

            if (estado.corte != esperado)
                return "corte (" + std::to_string(estado.corte) + ") difere do recalculado (" + std::to_string(esperado) + ")";

            for (int32_t v = 0; v < grafo.qtd_vertices; ++v) {
                const int32_t d = calcularDelta(grafo, estado.particao, v);
                if (estado.delta[v] != d)
                    return "delta do vértice " + std::to_string(v) + 
                    " SA (" + std::to_string(estado.delta[v]) + 
                    ") difere do recalculado (" + std::to_string(d) + ")";
            }

            return ""; 
        }

        void validarParametros(const Parametros &p) {
            if (!(p.T1 > 0 && p.T1 <= p.T0))
                throw std::invalid_argument("temperaturas devem satisfazer 0 < T1 <= T0 (T0=" + std::to_string(p.T0) +
                                            ", T1=" + std::to_string(p.T1) + ")");
            if (!p.guiadoPorTempo && p.varreduras < 1)
                throw std::invalid_argument("o número de varreduras deve ser >= 1");
        }

        double segundos(Relogio::duration d) {
            return std::chrono::duration<double>(d).count();
        }

    }  // namespace

    // temperatuda do resfriamento geometrico para uma fracao f em [0, 1] do caminho
    // ja percorrido: T(f) = T0 * (T1/T0)^f, então f = 0 da T0 e f = 1 dá T1.
    // É o calculo real da curva; os dois modos do SA usam esta função.
    // No modo por tempo, f vem do relógio (tempo decorrido / duração total).
    double temperaturaPorFracao(
        double f,
        double T0,
        double T1
    ) {
        return T0 * std::pow(T1 / T0, f);
    }

    // temperatura da varredura s (de 0 a S-1) no modo de varreduras fixas.
    // Só converte "varredura s de S" na fração f = s/(S-1) e delega o calculo
    // para temperaturaPorFracao.
    double temperaturaGeometrica(
        long s,
        long S,
        double T0,
        double T1
    ) {
        // com uma única varredura não há "caminho" a percorrer (S-1 seria 0): fica em T0
        if (S <= 1) return T0;
        return temperaturaPorFracao(static_cast<double>(s) / static_cast<double>(S - 1), T0, T1);
    }

    Resultado executar(
        const grafo::Grafo &grafoOriginal,
        const Parametros &param
    ) {
        validarParametros(param);

        const auto inicio = Relogio::now();

        coloracao::GrafoColorido colorido;
        if (param.colorida) {
            const coloracao::Coloracao cores = coloracao::colorir(grafoOriginal);
            const std::string problema = coloracao::verificar(grafoOriginal, cores);

            if (!problema.empty()) throw std::logic_error("coloracao invalida: " + problema);

            colorido = coloracao::organizarPorCor(grafoOriginal, cores);
            const std::string problemaOrganizado = coloracao::verificar(colorido.grafo, colorido.coloracao);

            if (!problemaOrganizado.empty()) throw std::logic_error("coloracao organizada invalida: " + problemaOrganizado);
        }
        const coloracao::Coloracao &cores = colorido.coloracao;
        const grafo::Grafo &grafo = param.colorida ? colorido.grafo : grafoOriginal;

        rng::Gerador gerador(param.seed);
        Estado e = estadoInicial(grafo, gerador);

        // adjacencia compactada feita uma vez 
        const std::vector<uint32_t> adjacencia = compactarAdjacencia(grafo);
        // tabela de limiares refeita a cada mudanca de temperatura (grau_max + 1 posicoes)
        std::vector<uint32_t> limiares(grafo.grauMaximo() + 1);
        
        Resultado r;
        r.corteInicial = e.corte;
        r.corteMelhor = e.corte;
        r.melhor = e.particao;

        r.numCores = param.colorida ? cores.numCores : 0;

        // uma varredura, sequencial ou colorida
        auto varrerUma = [&]() {
            if (param.colorida) 
                varrerColorido(grafo, adjacencia, cores, limiares, e, gerador(), param.threads);
            else 
                varrer(grafo, adjacencia, limiares, e, gerador);
        };

        // traco opcional da trajetoria
        auto escreverTraco = [&](double temp, int64_t corte, int64_t melhorCorte) {
            if (!param.traco) return;
            *param.traco << r.varreduras << ',' << segundos(Relogio::now() - inicio) << ',' << temp << ','
                         << corte << ',' << melhorCorte << '\n';
        };
        if (param.traco) *param.traco << "varredura,segundos,temperatura,corte,melhor\n";

        // guarda a melhor particao vista ao fim de cada varredura (so copia quando melhora)
        auto registrarMelhor = [&]() {
            if (e.corte > r.corteMelhor) {
                r.corteMelhor = e.corte;
                r.melhor = e.particao;
                r.varreduraDaMelhor = r.varreduras;
            }
        };

        // ponto de controle: entrega a melhor particao vista (ids originais) a cada intervaloPonto segundos.
        const bool temPonto = param.intervaloPonto > 0 && static_cast<bool>(param.aoSalvarPonto);
        auto ultimoPonto = Relogio::now();
        int64_t corteDoPonto = LLONG_MIN;

        auto salvarPonto = [&](bool forcar) {
            if (!temPonto) return;

            const auto agora = Relogio::now();
            
            if (!forcar && segundos(agora - ultimoPonto) < param.intervaloPonto) 
                return;
            
            ultimoPonto = agora;
            
            if (r.corteMelhor == corteDoPonto) 
                return;
            
            corteDoPonto = r.corteMelhor;

            Estado copia = estadoDaParticao(grafo, r.melhor);
            descidaGulosa(grafo, adjacencia, copia);

            if (param.colorida) {
                std::vector<uint8_t> original(grafoOriginal.qtd_vertices);
                for (int32_t v = 0; v < grafoOriginal.qtd_vertices; ++v) original[v] = copia.particao[colorido.novoId[v]];
                param.aoSalvarPonto(original, copia.corte);
            } else {
                param.aoSalvarPonto(copia.particao, copia.corte);
            }
        };
        salvarPonto(true);      // logo no inicio: ja existe uma saida valida

        if (!param.guiadoPorTempo) {
            // varreduras fixas: T depende só do índice da varredura (determinístico)
            const long passoTraco = std::max(1L, param.varreduras / std::max(1L, param.pontosTraco));

            for (long s = 0; s < param.varreduras; ++s) {
                const double temp = temperaturaGeometrica(s, param.varreduras, param.T0, param.T1);
                montarLimiares(limiares, temp);
                varrerUma();
                ++r.varreduras;
                registrarMelhor();
                salvarPonto(false);

                if (r.varreduras % passoTraco == 0) escreverTraco(temp, e.corte, r.corteMelhor);
            }
        } else {
            // guiado por tempo: T = T0 * (T1/T0)^(fração do tempo decorrido até o prazo)
            const auto t0 = Relogio::now();
            const double duracao = segundos(param.prazo - t0);
            const long porConsulta = std::max(1L, VISITAS_ENTRE_CONSULTAS / std::max<long>(1, grafo.qtd_vertices));

            double proximoTraco = 0.0;      // fracao do resfriamento em que sai a proxima linha do traco

            while (duracao > 0) {
                const double fracao = segundos(Relogio::now() - t0) / duracao;
                if (fracao >= 1.0) break;

                const double temp = temperaturaPorFracao(fracao, param.T0, param.T1);
                montarLimiares(limiares, temp);

                if (fracao >= proximoTraco) {
                    escreverTraco(temp, e.corte, r.corteMelhor);
                    proximoTraco += 1.0 / static_cast<double>(std::max(1L, param.pontosTraco));
                }

                for (long k = 0; k < porConsulta; ++k) {
                    varrerUma();

                    ++r.varreduras;
                    registrarMelhor();
                }
                salvarPonto(false);
            }
        }

        r.corteFinal = e.corte;     // estado ao fim do resfriamento, antes da descida
        escreverTraco(param.T1, e.corte, r.corteMelhor);

        // polimento final
        Estado melhor = estadoDaParticao(grafo, r.melhor);
        descidaGulosa(grafo, adjacencia, e);
        descidaGulosa(grafo, adjacencia, melhor);

        if (melhor.corte > e.corte) {
            r.corteDescida = melhor.corte;
            r.melhor = melhor.particao;
        } else {
            r.corteDescida = e.corte;
            r.melhor = e.particao;
        }

        // ultima linha do traco: o resultado final, apos a descida gulosa
        escreverTraco(param.T1, r.corteDescida, r.corteDescida);

        r.segundos = segundos(Relogio::now() - inicio);

        r.aviso = verificarEstado(grafo, e);
        if (r.aviso.empty()) r.aviso = verificarEstado(grafo, melhor);

        if (param.colorida) {
            // volta aos ids originais e confere o corte no grafo ORIGINAL
            std::vector<uint8_t> original(grafoOriginal.qtd_vertices);
            for (int32_t v = 0; v < grafoOriginal.qtd_vertices; ++v) original[v] = r.melhor[colorido.novoId[v]];

            const int64_t corteOriginal = grafo::valorDoCorte(grafoOriginal, original);
            if (corteOriginal != r.corteDescida && r.aviso.empty())
                r.aviso = "corte no grafo original (" + std::to_string(corteOriginal) + ") difere do calculado no grafo organizado (" +
                          std::to_string(r.corteDescida) + ")";
            r.melhor = std::move(original);
        }
        return r;
    }

}  // namespace sa