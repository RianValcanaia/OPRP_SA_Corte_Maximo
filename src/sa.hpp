/*
    sa.hpp — Simulated Annealing
 */
#pragma once

#include <cstdint>
#include <vector>

#include "grafo.hpp"

namespace sa {

    struct Parametros {
        double T0 = 1.5;          // temperatura inicial
        double T1 = 0.3;          // temperatura final
        long varreduras = 10000;  // numero de varreduras
        uint64_t seed = 12345;
    };

    struct Resultado {
        std::vector<uint8_t> melhor;   // melhor particao vista
        int64_t corteInicial = 0;      // corte da particao inicial
        int64_t corteMelhor = 0;       // corte da melhor particao
        int64_t corteFinal = 0;        // corte do estado ao fim da ultima varredura
        long varreduraDaMelhor = 0;    
        double segundos = 0;           // tempo gasto somente no SA
    };

    // T da varredura s (0 <= s < S): T0 * (T1/T0)^(s/(S-1)). Com S = 1 devolve T0.
    double temperaturaGeometrica(long s, long S, double T0, double T1);

    // Roda o SA.
    Resultado executar(
        const grafo::Grafo &g, 
        const Parametros &param);
        
}  // namespace sa
