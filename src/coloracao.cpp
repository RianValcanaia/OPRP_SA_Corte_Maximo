#include "coloracao.hpp"

#include <algorithm>

namespace coloracao {

    namespace {
        std::vector<int32_t> ordemDeRemocao(
            const grafo::Grafo &grafo
        ) {
            const int32_t n = grafo.qtd_vertices;
            
            // copia o grau e grauMax
            std::vector<int32_t> grau(n);
            for (int32_t v = 0; v < n; ++v) grau[v] = grafo.grau(v);
            int32_t grauMax = grafo.grauMaximo();
            
            std::vector<int32_t> inicioDoGrau(grauMax + 2, 0);
            for (int32_t v = 0; v < n; ++v) ++inicioDoGrau[grau[v] + 1];     // mesma ideia que o offset so que para graus
            for (int32_t d = 1; d <= grauMax + 1; ++d) inicioDoGrau[d] += inicioDoGrau[d - 1];    // soma acumulada
            
            std::vector<int32_t> ordem(n);  // ordem[p] = vertice na posicao p
            std::vector<int32_t> pos(n);    // pos[v] = posicao do vertice v na fila
            std::vector<int32_t> proximo(inicioDoGrau.begin(), inicioDoGrau.end() - 1);  // proximo livre

            // distribui cada vertice na proxima vaga do grupo do seu grau
            for (int32_t v = 0; v < n; ++v) {
                pos[v] = proximo[grau[v]]++;    // pega a vaga e avanca o cursor do grupo
                ordem[pos[v]] = v;              // coloca v nessa vaga da fila
            }

            for (int32_t i = 0; i < n; ++i) {
                const int32_t v = ordem[i];     // vertice de menor grau restante

                for (int32_t k = grafo.offsets[v]; k < grafo.offsets[v + 1]; ++k) {
                    const int32_t u = grafo.vizinhos[k];
                    if (grau[u] <= grau[v]) continue;       // u ja saiu ou ja tem grau minimo -> nada a mover

                    // move u para o inicio do seu grupo de grau e diminui seu grau em 1
                    const int32_t du = grau[u], pu = pos[u], pw = inicioDoGrau[du];
                    const int32_t w = ordem[pw];
                    if (u != w) {
                        std::swap(ordem[pu], ordem[pw]);
                        pos[u] = pw;
                        pos[w] = pu;
                    }
                    ++inicioDoGrau[du];
                    --grau[u];
                }
            }
            return ordem;
        }
    }  // namespace

    Coloracao colorir(
        const grafo::Grafo &grafo
    ) {
        const int32_t n = grafo.qtd_vertices;
        const std::vector<int32_t> remocao = ordemDeRemocao(grafo);

        Coloracao c;
        // inicia com todas as posicoes em -1
        c.cor.assign(n, -1);

        
        // marca as cores já usadas
        std::vector<int32_t> marca(grafo.grauMaximo() + 2, -1);
        
        // colore na ordem inversa da remocao, sempre com a menor cor que nenhum vizinho ja usa
        for (int32_t i = n - 1; i >= 0; --i) {
            const int32_t v = remocao[i];

            for (int32_t k = grafo.offsets[v]; k < grafo.offsets[v + 1]; ++k) {
                const int32_t cu = c.cor[grafo.vizinhos[k]];
                if (cu >= 0) marca[cu] = v;
            }

            int32_t cv = 0;
            while (marca[cv] == v) ++cv;
            c.cor[v] = cv;
            c.numCores = std::max(c.numCores, cv + 1);
        }

        // agrupa os vértices por cor (contagem + soma de prefixos); dentro de cada cor ficam em ordem crescente
        c.inicio.assign(c.numCores + 1, 0);
        for (int32_t v = 0; v < n; ++v) ++c.inicio[c.cor[v] + 1];
        for (int32_t k = 1; k <= c.numCores; ++k) c.inicio[k] += c.inicio[k - 1];

        c.vertices.resize(n);
        std::vector<int32_t> cursor(c.inicio.begin(), c.inicio.end() - 1);
        for (int32_t v = 0; v < n; ++v) c.vertices[cursor[c.cor[v]]++] = v;

        return c;
    }

    GrafoColorido organizarPorCor(
        const grafo::Grafo &grafo, 
        const Coloracao &c
    ) {
        const int32_t n = grafo.qtd_vertices;
        GrafoColorido gc;

        // o vertice original c.vertices[t] vira o vertice t
        gc.novoId.resize(n);
        for (int32_t t = 0; t < n; ++t) gc.novoId[c.vertices[t]] = t;

        grafo::Grafo &g = gc.grafo;
        g.qtd_vertices = n;
        g.qtd_arestas = grafo.qtd_arestas;
        g.offsets.resize(n + 1);
        g.offsets[0] = 0;

        for (int32_t t = 0; t < n; ++t) g.offsets[t + 1] = g.offsets[t] + grafo.grau(c.vertices[t]);

        g.vizinhos.resize(grafo.vizinhos.size());
        g.peso.resize(grafo.peso.size());
        for (int32_t t = 0; t < n; ++t) {
            const int32_t v = c.vertices[t];
            int32_t destino = g.offsets[t];
            
            for (int32_t k = grafo.offsets[v]; k < grafo.offsets[v + 1]; ++k, ++destino) {
                g.vizinhos[destino] = gc.novoId[grafo.vizinhos[k]];
                g.peso[destino] = grafo.peso[k];
            }
        }

        // coloracao do grafo organizado: as cores ficam nos mesmos intervalos, e os vertices viram 0..n-1
        gc.coloracao.numCores = c.numCores;
        gc.coloracao.inicio = c.inicio;
        gc.coloracao.vertices.resize(n);
        gc.coloracao.cor.resize(n);

        for (int32_t t = 0; t < n; ++t) {
            gc.coloracao.vertices[t] = t;
            gc.coloracao.cor[t] = c.cor[c.vertices[t]];
        }
        return gc;
    }

    std::string verificar(
        const grafo::Grafo &grafo, 
        const Coloracao &c
    ) {
        const int32_t n = grafo.qtd_vertices;

        // os vetores tem o tamanho esperado? (cor e vertices: n posicoes; inicio: numCores + 1)
        if (static_cast<int32_t>(c.cor.size()) != n ||
            static_cast<int32_t>(c.vertices.size()) != n ||
            static_cast<int32_t>(c.inicio.size()) != c.numCores + 1) return "tamanhos inconsistentes";

        for (int32_t v = 0; v < n; ++v) {
            // a cor de cada vertice esta em [0, numCores)?
            if (c.cor[v] < 0 || c.cor[v] >= c.numCores)
                return "vértice " + std::to_string(v) + " com cor fora do intervalo: " + std::to_string(c.cor[v]);

            // verifica se nenhum vizinho de v tem a mesma cor que v
            for (int32_t k = grafo.offsets[v]; k < grafo.offsets[v + 1]; ++k)
                if (c.cor[grafo.vizinhos[k]] == c.cor[v])
                    return "vértices vizinhos " + std::to_string(v) + " e " + std::to_string(grafo.vizinhos[k]) +
                           " com a mesma cor " + std::to_string(c.cor[v]);
        }

        // as listas por cor, cada vertice aparece uma vez, na cor certa
        std::vector<uint8_t> visto(n, 0);       // visto[v] = 1 se v ja apareceu em alguma lista
        for (int32_t cor = 0; cor < c.numCores; ++cor) {
            for (int32_t t = c.inicio[cor]; t < c.inicio[cor + 1]; ++t) {
                const int32_t v = c.vertices[t];
                if (v < 0 || v >= n) // id inexistente
                    return "vértice inválido na lista de cores";

                if (visto[v]) // apareceu duas vezes
                    return "vértice " + std::to_string(v) + " repetido nas listas de cores";

                if (c.cor[v] != cor) // lista da cor errada
                    return "vértice " + std::to_string(v) + " listado na cor errada";

                visto[v] = 1;
            }
        }

        // nenhum vertice ficou de fora das listas
        for (int32_t v = 0; v < n; ++v)
            if (!visto[v]) return "vértice " + std::to_string(v) + " ausente das listas de cores";

        return ""; // tudo certo
    }
}  // namespace coloracao