#include "grafo.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace grafo {

// namespace privado
namespace {

// Lr o arquivo inteiro para a memoria.
std::vector<char> lerArquivoInteiro(
    const std::string &caminho
) {
   
    FILE *f = std::fopen(caminho.c_str(), "rb");
    if (!f) throw std::runtime_error("não foi possível abrir '" + caminho + "'");

    std::vector<char> dados;
 
    if (std::fseek(f, 0, SEEK_END) == 0) {
        long tamanho = std::ftell(f);
        std::rewind(f);
        if (tamanho > 0) dados.resize(static_cast<size_t>(tamanho));
    }

    // Caminho normal: tamanho conhecido, uma única leitura.
    size_t lidos = dados.empty() ? 0 : std::fread(dados.data(), 1, dados.size(), f);
    dados.resize(lidos);

    // Se o tamanho não era conhecido, le o que sobrar em blocos.
    char bloco[1 << 16];
    size_t k;
    while ((k = std::fread(bloco, 1, sizeof bloco, f)) > 0) 
        dados.insert(dados.end(), bloco, bloco + k);

    std::fclose(f);
    return dados;
}

// Leitor de inteiros sobre um buffer em memória.
class Leitor {
    public:
        // contrutor da classe 
        Leitor(const std::vector<char> &dados) {
            _inicio   = dados.data();
            _fim = dados.data() + dados.size();
        }

        // Le o próximo inteiro, retorna false se acabou
        bool proximo(int64_t &valor) {
            
            // Pula espacos em branco, avanca até o primeiro caractere util
            while (_inicio < _fim && (*_inicio == ' ' || *_inicio == '\n' || *_inicio == '\r' || *_inicio == '\t')) ++_inicio;

            if (_inicio >= _fim) return false;

            bool negativo = false;
            if (*_inicio == '-' || *_inicio == '+') {
                negativo = (*_inicio == '-');
                ++_inicio;
            }

            // garante que leu um digito
            if (_inicio >= _fim || *_inicio < '0' || *_inicio > '9') return false;

            int64_t v = 0;
            while (_inicio < _fim && *_inicio >= '0' && *_inicio <= '9') {
                v = v * 10 + (*_inicio - '0');
                if (v > (int64_t{1} << 40)) return false;
                ++_inicio;
            }
            
            if (_inicio < _fim && *_inicio != ' ' && *_inicio != '\n' && *_inicio != '\r' && *_inicio != '\t') return false;

            valor = negativo ? -v : v;
            return true;
        }

    private:
        const char *_inicio;
        const char *_fim;
};

}  // namespace

// Maior grau do grafo (maior número de vizinhos de um vértice).
int32_t Grafo::grauMaximo() const {
    int32_t maior = 0;
    for (int32_t v = 0; v < qtd_vertices; ++v) maior = std::max(maior, grau(v));
    return maior;
}

Grafo lerArquivo(
    const std::string &caminho
) {
    const std::vector<char> dados = lerArquivoInteiro(caminho);
    Leitor leitor(dados);

    int64_t qtd_vertices, qtd_arestas;
    if (!leitor.proximo(qtd_vertices) || !leitor.proximo(qtd_arestas))
        throw std::runtime_error("cabeçalho inválido em '" + caminho + "' (esperado: n m)");

    if (qtd_vertices < 1 || qtd_vertices > INT32_MAX - 1 || qtd_arestas < 0)
        throw std::runtime_error("cabeçalho fora dos limites (n=" + std::to_string(qtd_vertices) + ", m=" + std::to_string(qtd_arestas) + ")");

    if (2 * qtd_arestas > INT32_MAX)
        throw std::runtime_error("grafo grande demais para índices de 32 bits (m=" + std::to_string(qtd_arestas) + ")");

    // primeira passada le as arestas para vetores temp e conta os graus
    std::vector<int32_t> us(qtd_arestas), vs(qtd_arestas);
    std::vector<int8_t> ws(qtd_arestas);
    std::vector<int32_t> offsets(qtd_vertices + 2, 0);

    for (int64_t i = 0; i < qtd_arestas; ++i) {
        int64_t u, v, w;
        
        if (!leitor.proximo(u) || !leitor.proximo(v) || !leitor.proximo(w))
            throw std::runtime_error("aresta " + std::to_string(i + 1) + " ausente ou incorreta" );

        if (u < 1 || u > qtd_vertices || v < 1 || v > qtd_vertices)
            throw std::runtime_error("aresta " + std::to_string(i + 1) + ": vértice fora de [1, " + std::to_string(qtd_vertices) + "]");

        if (w != 1 && w != -1)
            throw std::runtime_error("aresta " + std::to_string(i + 1) + ": peso " + std::to_string(w) + " (esperado 1 ou -1)");

        us[i] = static_cast<int32_t>(u - 1);
        vs[i] = static_cast<int32_t>(v - 1);
        ws[i] = static_cast<int8_t>(w);

        if (u != v) {
            ++offsets[us[i] + 2];
            ++offsets[vs[i] + 2];
        }
    }

    // offsets[v+2] = grau de v
    for (int64_t v = 2; v <= qtd_vertices + 1; ++v) offsets[v] += offsets[v - 1];

    // segunda passada: preenche vizinhos/peso;
    const int64_t entradas = offsets[qtd_vertices + 1];
    std::vector<int32_t> vizinhos(entradas);
    std::vector<int8_t> peso(entradas);
    
    for (int64_t i = 0; i < qtd_arestas; ++i) {
        if (us[i] == vs[i]) continue;

        // proxima posicao livre na lista de u
        int32_t pu = offsets[us[i] + 1]++;
        vizinhos[pu] = vs[i];      // em u o vizinho é v
        peso[pu] = ws[i]; 
        
        // proxima posicao livre na lista de v
        int32_t pv = offsets[vs[i] + 1]++;
        vizinhos[pv] = us[i];     // em v o vizinho é u
        peso[pv] = ws[i];
    }
    offsets.pop_back();  // descarta a posição extra: ficam n + 1 offsets

    Grafo g;
    g.qtd_vertices = static_cast<int32_t>(qtd_vertices);
    g.qtd_arestas = qtd_arestas;
    g.offsets = std::move(offsets);
    g.vizinhos = std::move(vizinhos);
    g.peso = std::move(peso);
    return g;
}

int64_t valorDoCorte(
    const Grafo &grafo,
    const std::vector<uint8_t> &particao
) {
    int64_t soma = 0;
    for (int32_t i = 0; i < grafo.qtd_vertices; ++i)
        for (int32_t j = grafo.offsets[i]; j < grafo.offsets[i + 1]; ++j)
            if (particao[j] != particao[grafo.vizinhos[j]]) soma += grafo.peso[j];
    return soma / 2;
}

}  // namespace grafo
