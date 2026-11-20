#include "sa.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>

#include "random.hpp"

namespace sa {

    namespace {
        // estado do SA: particao, ganho de cada vertice e valor do corte
        struct Estado {
            std::vector<uint8_t> x;
            std::vector<int32_t> delta;
            int64_t corte = 0;
        };

        // ganho de trocar o vertice v de lado, calculado do zero a partir dos vizinhos
        int32_t calcularDelta(
            const grafo::Grafo &g, 
            const std::vector<uint8_t> &x, 
            int32_t v
        ) {
            int32_t soma = 0;
            for (int32_t k = g.offsets[v]; k < g.offsets[v + 1]; ++k)
                soma += g.peso[k] * (x[g.vizinhos[k]] == x[v] ? 1 : -1);
            return soma;
        }

        // particao aleatoria + delta de todos os vertices + corte, tudo calculado do zero.
        Estado estadoInicial(
            const grafo::Grafo &g, 
            rng::Gerador &gerador
        ) {
            Estado e;
            e.x.resize(g.qtd_vertices);

            for (auto &lado : e.x) lado = static_cast<uint8_t>(gerador() & 1u);

            e.delta.resize(g.qtd_vertices);

            for (int32_t v = 0; v < g.qtd_vertices; ++v) e.delta[v] = calcularDelta(g, e.x, v);

            e.corte = grafo::valorDoCorte(g, e.x);

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

            uint8_t *x = estado.x.data();
            int32_t *delta = estado.delta.data();

            for (int32_t i = 0; i < qtd_vertices; ++i) {
                const int32_t d = delta[i];

                // Metropolis: ganho >= 0 sempre aceita; piora (d < 0) aceita com prob. e^(d/T).
                if (d < 0 && !(rng::sortearReal01(gerador) < std::exp(d / temp))) continue;

                // aplica o movimento
                estado.corte += d;
                x[i] ^= 1u;
                delta[i] = -d;

                // Cada vizinho j enxerga a aresta (i,j) com o novo lado de i: o termo dela em
                // delta[j] troca de sinal, ou seja, muda em 2 * (novo termo).
                const uint8_t xi = x[i];
                for (int32_t k = offsets[i]; k < offsets[i + 1]; ++k) {
                    const int32_t j = vizinhos[k];
                    delta[j] += 2 * peso[k] * (x[j] == xi ? 1 : -1);
                }
            }
        }

        // convere o estado incremental contra um recalculo completo.
        void verificarEstado(
            const grafo::Grafo &grafo, 
            const Estado &estado
        ) {
            const int64_t esperado = grafo::valorDoCorte(grafo, estado.x);
            if (estado.corte != esperado)
                throw std::logic_error("corte incremental (" + std::to_string(estado.corte) + ") difere do recalculado (" +
                                    std::to_string(esperado) + ")");

            for (int32_t v = 0; v < grafo.qtd_vertices; ++v) {
                const int32_t d = calcularDelta(grafo, estado.x, v);
                if (estado.delta[v] != d)
                    throw std::logic_error("delta do vértice " + std::to_string(v) + " incremental (" +
                                        std::to_string(estado.delta[v]) + ") difere do recalculado (" + std::to_string(d) + ")");
            }
        }

        void validarParametros(const Parametros &p) {
            if (!(p.T1 > 0 && p.T1 <= p.T0))
                throw std::invalid_argument("temperaturas devem satisfazer 0 < T1 <= T0 (T0=" + std::to_string(p.T0) +
                                            ", T1=" + std::to_string(p.T1) + ")");
            if (p.varreduras < 1) throw std::invalid_argument("o número de varreduras deve ser >= 1");
        }

    }  // namespace

    double temperaturaGeometrica(
        long s, 
        long S, 
        double T0, 
        double T1
    ) {
        if (S <= 1) return T0;
        return T0 * std::pow(T1 / T0, static_cast<double>(s) / static_cast<double>(S - 1));
    }

    Resultado executar(
        const grafo::Grafo &grafo, 
        const Parametros &param
    ) {
        validarParametros(param);

        const auto inicio = std::chrono::steady_clock::now();
        rng::Gerador gerador(param.seed);
        Estado e = estadoInicial(grafo, gerador);

        Resultado r;
        r.corteInicial = e.corte;
        r.corteMelhor = e.corte;
        r.melhor = e.x;

        for (long s = 0; s < param.varreduras; ++s) {
            varrer(grafo, e, temperaturaGeometrica(s, param.varreduras, param.T0, param.T1), gerador);

            // guarda a melhor partição vista ao fim de cada varredura (só copia quando melhora)
            if (e.corte > r.corteMelhor) {
                r.corteMelhor = e.corte;
                r.melhor = e.x;
                r.varreduraDaMelhor = s + 1;
            }
        }
        r.corteFinal = e.corte;
        r.segundos = std::chrono::duration<double>(std::chrono::steady_clock::now() - inicio).count();

        verificarEstado(grafo, e);
        return r;
    }

}  // namespace sa
