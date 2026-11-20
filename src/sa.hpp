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
#include <string>
#include <vector>

#include "grafo.hpp"

namespace sa {

    using Relogio = std::chrono::steady_clock;

    struct Parametros {
        double T0 = 1.5;                // temperatura inicial
        double T1 = 0.3;                // temperatura final
        long varreduras = 10000;        // numero de varreduras (só no modo de varreduras fixas)
        bool guiadoPorTempo = false;    // true: T segue o relógio até 'prazo'
        Relogio::time_point prazo{};    // instante em que o SA deve terminar 
        uint64_t seed = 12345;
    };

    struct Resultado {
        std::vector<uint8_t> melhor;   // melhor particao vista (ao fim de uma varredura)
        int64_t corteInicial = 0;      // corte da particao inicial
        int64_t corteMelhor = 0;       // corte da melhor particao
        int64_t corteFinal = 0;        // corte do estado ao fim da ultima varredura
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
