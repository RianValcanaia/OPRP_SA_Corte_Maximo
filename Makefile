#   make            compila o programa (./maxcut)
#   make clean      apaga build/ e o executavel
#
# Uso: ./maxcut <grafo.txt> <saida.txt> [--tempo S] [--varreduras N] [--seed N] [--t0 X] [--t1 X] [--trace ARQ.csv]

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -fopenmp

FONTES  := $(wildcard src/*.cpp)
OBJETOS := $(patsubst src/%.cpp,build/%.o,$(FONTES))
DEPS    := $(OBJETOS:.o=.d)

.PHONY: all testes clean

all: maxcut

maxcut: $(OBJETOS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# -MMD -MP gera build/<arquivo>.d com as dependencias de cabecalho: mexeu em um .hpp, recompila quem o inclui
build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

build:
	mkdir -p build

# verificador aux/
build/verificador: aux/verificador.c | build
	gcc -O2 -Wall -Wextra -o $@ $<

clean:
	rm -rf build maxcut

-include $(DEPS)
