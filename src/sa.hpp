/*
    sa.hpp — Simulated Annealing sequencial para Max-Cut

    O estado e a partição. Um movimento troca o lado de um vértice i; seu ganho
    delta[i] eh guardado e so muda quando um vizinho troca de lado.
    Aceita o movimento se delta >= 0 ou, senão, com probabilidade e^(delta/T).

    Dois modos de resfriamento (T0 -> T1, geométrico):
        - varreduras fixas;
        - guiado por tempo: T acompanha o relógio até o prazo, T = T0 * (T1/T0)^(fração do 
        tempo decorrido).
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include "grafo.hpp"

namespace sa {

    using Relogio = std::chrono::steady_clock;

    struct Parametros {
        double T0 = 2.0;                // temperatura inicial
        double T1 = 0.15;               // temperatura final 
        long varreduras = 10000;        // numero de varreduras 
        bool guiadoPorTempo = false;    // true: T segue o relógio até 'prazo'
        Relogio::time_point prazo{};    // instante em que o SA deve terminar 
        uint64_t seed = 12345;
        bool colorida = false;          // true = varredura colorida
        int threads = 1;                // threads openmp da varredura colorida 
        double intervaloPonto = 0;
        std::function<void(const std::vector<uint8_t> &particao, int64_t corte)> aoSalvarPonto;
        std::ostream *traco = nullptr;  // se != nullptr, escreve aqui a trajetoria do SA em CSV
        long pontosTraco = 1000;        // quantos pontos (linhas) o traco tem, aproximadamente
    };

    struct Resultado {
        std::vector<uint8_t> melhor;   // particao devolvida no final
        int64_t corteInicial = 0;      // corte da particao inicial
        int64_t corteMelhor = 0;       
        int64_t corteFinal = 0;        
        int64_t corteDescida = 0;      
        int numCores = 0;              // cores usadas pela coloracao (0 para sequencial)
        long varreduras = 0;           // varreduras realizadas
        long varreduraDaMelhor = 0;    // 0 = estado inicial; s = fim da s-ésima varredura
        double segundos = 0;           // tempo gasto somente no SA
        std::string aviso;             // não vazio se o estado incremental divergiu do recalculado
    };

    // T para uma fração f em [0, 1] do resfriamento: T0 * (T1/T0)^f.
    double temperaturaPorFracao(double f, double T0, double T1);

    // T da varredura s (0 <= s < S) no modo de varreduras fixas. Com S = 1 devolve T0.
    double temperaturaGeometrica(long s, long S, double T0, double T1);

    // Roda o SA. Sempre devolve uma partição valida
    Resultado executar(
        const grafo::Grafo &g,
        const Parametros &param);

}  // namespace sa
