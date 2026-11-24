#   make              compila os dois programas
#   make maxcut       so o ./maxcut (com opcoes, para experimentos)
#   make competicao   so o ./competicao (versao da competicao, sem opcoes)
#   make clean        apaga build/ e os executaveis
#
# Uso: mpirun -np N ./maxcut <grafo.txt> <saida.txt> [opcoes]
#      mpirun -np N ./competicao <grafo.txt> <saida.txt>

CXX      = mpicxx
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -fopenmp -DOMPI_SKIP_MPICXX

# fontes comuns aos dois programas (cada um tem o seu main)
COMUNS  := $(filter-out src/main.cpp src/competicao.cpp,$(wildcard src/*.cpp))
OBJ_COMUNS := $(patsubst src/%.cpp,build/%.o,$(COMUNS))
OBJETOS := $(OBJ_COMUNS) build/main.o build/competicao.o
DEPS    := $(OBJETOS:.o=.d)

.PHONY: all clean

all: maxcut competicao

maxcut: build/main.o $(OBJ_COMUNS)
	$(CXX) $(CXXFLAGS) -o $@ $^

competicao: build/competicao.o $(OBJ_COMUNS)
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
	rm -rf build maxcut competicao

-include $(DEPS)
