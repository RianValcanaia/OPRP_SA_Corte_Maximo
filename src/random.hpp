/*
    random.hpp — gerador de numeros aleatorios (xoshiro256++ - https://prng.di.unimi.it)
        - Gerador: gera inteiros de 64 bits;
        - sortearReal01: número real em [0, 1)
*/
#pragma once

#include <cstdint>

namespace rng {

    class Gerador {
        private:
            // rotacao circular a esquerda: os k bits que saem pela esquerda voltam pela direita
            static uint64_t rotacionar(uint64_t x, int k) {
                return (x << k) | (x >> (64 - k)); 
            }
            
            // estado interno do gerador (4 palavras de 64 bits)
            uint64_t _s[4];
            
            // embaralha um numero qualquer e avanca o estado
            static uint64_t splitmix64(uint64_t &estado) {
                uint64_t z = (estado += 0x9E3779B97F4A7C15ULL);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                return z ^ (z >> 31);
            }

        public:
            // A semente passa por splitmix64 para preencher os 4 estados 
            explicit Gerador(uint64_t semente) {
                for (int i = 0; i < 4; ++i) _s[i] = splitmix64(semente);
            }  
        
            // devolve o proximo numero de 64 bits e avanca o estado
            uint64_t operator()() {
                // saida 
                const uint64_t resultado = rotacionar(_s[0] + _s[3], 23) + _s[0];
                
                // atualizacao do estado, mistura as 4 palavras para a proxima chamada
                const uint64_t t = _s[1] << 17;
                _s[2] ^= _s[0];
                _s[3] ^= _s[1];
                _s[1] ^= _s[2];
                _s[0] ^= _s[3];
                _s[2] ^= t;
                _s[3] = rotacionar(_s[3], 45);

                return resultado;
            }
    };
}  // namespace rng
