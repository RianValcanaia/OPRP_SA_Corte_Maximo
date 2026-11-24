#include "mpi_util.hpp"

#include <mpi.h>
#include <omp.h>
#include <sched.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace dist {

    namespace {

        constexpr int TAG_PARTICAO = 17;

        // devolve quantas CPUs este processo tem permissao de usar
        int cpusNaMascara() {
            // vetor de bits, o bit i vale 1 se a CPU i eh permitida
            cpu_set_t atual;

            // pergunta ao Linux a mascara deste processo (0 = o proprio); devolve 0 se deu certo.
            if (sched_getaffinity(0, sizeof(atual), &atual) == 0)
                // CPU_COUNT conta os bits ligados
                return std::max(1, CPU_COUNT(&atual));

            // se a chamada falhar, quantas CPUs o OpenMP enxerga
            return std::max(1, omp_get_num_procs());
        }

        // devolve quantos nucleos fisicos este processo tem permissao de usar
        int nucleosFisicosNaMascara() {
            cpu_set_t atual;
            if (sched_getaffinity(0, sizeof(atual), &atual) != 0)
                return cpusNaMascara();
            
            std::set<std::string> nucleos;
            for (int i = 0; i < CPU_SETSIZE; ++i) {
                if (!CPU_ISSET(i, &atual))
                    continue;

                std::ifstream arq("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/topology/thread_siblings_list");
                std::string irmas;
                if (!(arq >> irmas))
                    return cpusNaMascara();     // sem /sys: usa as CPUs logicas

                nucleos.insert(irmas);
            }
            return std::max(1, static_cast<int>(nucleos.size()));
        }

        void prepararAmbienteEReiniciar(char **argv) {
            if (std::getenv("MAXCUT_JA_REINICIADO") != nullptr)
                return;     // evita laco infinito

            bool reiniciar = false;

            // O mpirun prende cada rank a poucos nucleos; liberamos todas as CPUs do no.
            if (std::getenv("OMPI_COMM_WORLD_SIZE") != nullptr) {
                
                // guarda quantas cpus havia
                const int antes = cpusNaMascara();
                cpu_set_t todas;
                CPU_ZERO(&todas);

                for (int i = 0; i < CPU_SETSIZE; ++i)
                    CPU_SET(i, &todas);

                // so reinicia se a mascara realmente alargou
                if (sched_setaffinity(0, sizeof(todas), &todas) == 0 && cpusNaMascara() > antes)
                    reiniciar = true;
            }

            // espera das threads nas barreiras 
            if (std::getenv("OMP_WAIT_POLICY") == nullptr && std::getenv("GOMP_SPINCOUNT") == nullptr) {
                setenv("GOMP_SPINCOUNT", "1000", 1);
                reiniciar = true;
            }

            if (!reiniciar)
                return;

            setenv("MAXCUT_JA_REINICIADO", "1", 1);
            execv("/proc/self/exe", argv);
            // se o exec falhar, segue assim mesmo (so o libgomp fica com a configuracao antiga)
        }

        // MPI_Bcast com contagem int: o grafo do trabalho cabe com folga
        void difundir(
            void *dados, 
            size_t quantidade, 
            MPI_Datatype tipo
        ) {
            if (quantidade > static_cast<size_t>(INT_MAX)) 
                throw std::runtime_error("grafo grande demais para MPI_Bcast");

            MPI_Bcast(dados, static_cast<int>(quantidade), tipo, 0, MPI_COMM_WORLD);
        }

    }  // namespace

    Ambiente iniciar(int &argc, char **&argv) {
        prepararAmbienteEReiniciar(argv);

        int fornecido = 0;
        MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &fornecido);

        Ambiente amb;
        MPI_Comm_rank(MPI_COMM_WORLD, &amb.rank);
        MPI_Comm_size(MPI_COMM_WORLD, &amb.tamanho);

        // ranks que estão no mesmo no 
        MPI_Comm no;
        MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &no);
        MPI_Comm_size(no, &amb.tamanhoLocal);
        MPI_Comm_free(&no);

        amb.cpusPorRank = std::max(1, cpusNaMascara() / amb.tamanhoLocal);
        amb.nucleosPorRank = std::max(1, nucleosFisicosNaMascara() / amb.tamanhoLocal);
        return amb;
    }

    void finalizar() {
        MPI_Finalize();
    }

    std::string difundirGrafo(
        const Ambiente &amb, 
        grafo::Grafo &g, 
        const std::string &erroDaLeitura
    ) {
        if (amb.tamanho == 1) return erroDaLeitura;

        // o rank 0 conseguiu ler? senão todos param juntos
        int tamanhoErro = static_cast<int>(erroDaLeitura.size());

        MPI_Bcast(&tamanhoErro, 1, MPI_INT, 0, MPI_COMM_WORLD);
        
        if (tamanhoErro > 0) {
            std::string erro;

            if (amb.rank == 0) {
                erro = erroDaLeitura;                       // quem leu ja tem o texto
            } else {
                erro = std::string(tamanhoErro, '\0');      // os outros reservam espaco para receber
            }

            MPI_Bcast(&erro[0], tamanhoErro, MPI_CHAR, 0, MPI_COMM_WORLD);
            return erro;
        }

        // tamanhos e depois os tres vetores do CSR
        int64_t cabecalho[3] = {g.qtd_vertices, 
                                g.qtd_arestas, 
                                static_cast<int64_t>(g.vizinhos.size())
        };

        MPI_Bcast(cabecalho, 3, MPI_INT64_T, 0, MPI_COMM_WORLD);

        if (amb.rank != 0) {
            g.qtd_vertices = static_cast<int32_t>(cabecalho[0]);
            g.qtd_arestas = cabecalho[1];
            g.offsets.resize(static_cast<size_t>(cabecalho[0]) + 1);
            g.vizinhos.resize(static_cast<size_t>(cabecalho[2]));
            g.peso.resize(static_cast<size_t>(cabecalho[2]));
        }

        difundir(g.offsets.data(), g.offsets.size(), MPI_INT32_T);
        difundir(g.vizinhos.data(), g.vizinhos.size(), MPI_INT32_T);
        difundir(g.peso.data(), g.peso.size(), MPI_INT8_T);
        
        return "";
    }

    double maximoEntreRanks(double valor) {
        double maximo = valor;
        MPI_Allreduce(&valor, &maximo, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        return maximo;
    }

    uint64_t valorDoRank0(uint64_t valor) {
        MPI_Bcast(&valor, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
        return valor;
    }

    MelhorCorte melhorEntreRanks(int64_t corteLocal) {
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);

        struct { long valor; int rank; } entrada{static_cast<long>(corteLocal), rank}, saida{};
        MPI_Allreduce(&entrada, &saida, 1, MPI_LONG_INT, MPI_MAXLOC, MPI_COMM_WORLD);
        return {saida.valor, saida.rank};
    }

    void enviarParticaoAoRank0(
        const Ambiente &amb, 
        int origem, 
        std::vector<uint8_t> &particao
    ) {
        if (origem == 0) return;

        if (amb.rank == origem) {
            MPI_Send(particao.data(), static_cast<int>(particao.size()), MPI_UINT8_T, 0, TAG_PARTICAO, MPI_COMM_WORLD);
        } else if (amb.rank == 0) {
            MPI_Recv(particao.data(), static_cast<int>(particao.size()), MPI_UINT8_T, origem, TAG_PARTICAO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    std::vector<int64_t> reunirCortes(
        const Ambiente &amb, 
        int64_t corteLocal
    ) {
        std::vector<int64_t> todos(amb.rank == 0 ? amb.tamanho : 0);
        MPI_Gather(&corteLocal, 1, MPI_INT64_T, todos.data(), 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
        return todos;
    }

}  // namespace dist
