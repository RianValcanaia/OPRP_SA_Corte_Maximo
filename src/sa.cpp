#include "sa.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "random.hpp"

namespace sa {

    namespace {
        // quantos vertices visitar, no minimo, entre duas consultas ao relogio, para nao 
        // sobrecarregar com consultas.
        constexpr long VISITAS_ENTRE_CONSULTAS = 20000;

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

        // particao aleatoria + delta de todos os vertices + corte, tudo calculado do zero.
        Estado estadoInicial(
            const grafo::Grafo &grafo,
            rng::Gerador &gerador
        ) {
            Estado e;
            e.particao.resize(grafo.qtd_vertices);

            // sorteia o lado de cada vertice
            for (auto &lado : e.particao) 
                lado = static_cast<uint8_t>(gerador() & 1u);
            
            e.delta.resize(grafo.qtd_vertices);

            // calcula o vetor delta da particao inicial 
            for (int32_t v = 0; v < grafo.qtd_vertices; ++v) 
                e.delta[v] = calcularDelta(grafo, e.particao, v);

            e.corte = grafo::valorDoCorte(grafo, e.particao);

            return e;
        }

        void varrer(
            const grafo::Grafo &grafo,
            Estado &estado,
            double temp,
            rng::Gerador &gerador
        ) {
            const int32_t qtd_vertices = grafo.qtd_vertices;
            const int32_t *offsets = grafo.offsets.data();
            const int32_t *vizinhos = grafo.vizinhos.data();
            const int8_t *peso = grafo.peso.data();

            uint8_t *particao = estado.particao.data();
            int32_t *delta = estado.delta.data();

            for (int32_t i = 0; i < qtd_vertices; ++i) {
                const int32_t d = delta[i];

                // Metropolis: ganho >= 0 sempre aceita; piora (d < 0) aceita com prob. e^(d/T).
                if (d < 0 && !(rng::sortearReal01(gerador) < std::exp(d / temp))) continue;

                // aplica o movimento
                estado.corte += d;
                
                // inverte o bit
                particao[i] = !particao[i];
                
                // troca de sinal
                delta[i] = -d;

                // atualiza o delta dos vizinhos de i
                const uint8_t particao_i = particao[i];
                for (int32_t k = offsets[i]; k < offsets[i + 1]; ++k) {
                    const int32_t j = vizinhos[k];
                    delta[j] += peso[k] * 2  * (particao[j] == particao_i ? 1 : -1);
                }
            }
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
        const grafo::Grafo &grafo,
        const Parametros &param
    ) {
        validarParametros(param);

        const auto inicio = Relogio::now();
        rng::Gerador gerador(param.seed);
        Estado e = estadoInicial(grafo, gerador);

        Resultado r;
        r.corteInicial = e.corte;
        r.corteMelhor = e.corte;
        r.melhor = e.particao;

        // guarda a melhor particao vista ao fim de cada varredura (so copia quando melhora)
        auto registrarMelhor = [&]() {
            if (e.corte > r.corteMelhor) {
                r.corteMelhor = e.corte;
                r.melhor = e.particao;
                r.varreduraDaMelhor = r.varreduras;
            }
        };

        if (!param.guiadoPorTempo) {
            // varreduras fixas: T depende só do índice da varredura (determinístico)
            for (long s = 0; s < param.varreduras; ++s) {
                varrer(grafo, e, temperaturaGeometrica(s, param.varreduras, param.T0, param.T1), gerador);
                ++r.varreduras;
                registrarMelhor();
            }
        } else {
            // guiado por tempo: T = T0 * (T1/T0)^(fração do tempo decorrido até o prazo)
            const auto t0 = Relogio::now();
            const double duracao = segundos(param.prazo - t0);
            const long porConsulta = std::max(1L, VISITAS_ENTRE_CONSULTAS / std::max<long>(1, grafo.qtd_vertices));

            while (duracao > 0) {
                const double fracao = segundos(Relogio::now() - t0) / duracao;
                if (fracao >= 1.0) break;

                const double temp = temperaturaPorFracao(fracao, param.T0, param.T1);
                for (long k = 0; k < porConsulta; ++k) {
                    varrer(grafo, e, temp, gerador);
                    
                    ++r.varreduras;
                    registrarMelhor();
                }
            }
        }

        r.corteFinal = e.corte;
        r.segundos = segundos(Relogio::now() - inicio);

        r.aviso = verificarEstado(grafo, e);
        return r;
    }

}  // namespace sa
