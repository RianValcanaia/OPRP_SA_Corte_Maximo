#!/usr/bin/env python3
"""Bateria de testes de robustez do maxcut (só biblioteca padrão).

    make testes                                   # compila e roda tudo
    python3 scripts/testes.py                     # usa ./maxcut e build/verificador
    python3 scripts/testes.py --maxcut build_san/maxcut   # ex.: binário compilado com sanitizers

O que verifica:
  - entradas válidas de borda (n=1, sem arestas, só pesos +1 ou -1, arestas repetidas, laços, arquivo
    sem '\\n' final, fim de linha CRLF, espaços e linhas em branco extras, vértices em ordem invertida);
  - a saída tem sempre 1 linha com exatamente n caracteres 0/1 (conferido pelo aux/verificador) e o corte
    que o programa diz é o mesmo que o verificador calcula;
  - 60 grafos pequenos aleatórios: o corte tem de ser IGUAL ao ótimo por força bruta;
  - um grafo "estrela" (um vértice de grau 19.999: tabela de limiares enorme);
  - entradas inválidas e argumentos inválidos: o programa termina com código 1 e mensagem "erro:", nunca
    com falha de segmentação nem sinal, e não passa de 3 s com --tempo pequeno.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent


# ------------------------------------------------------------------ utilidades
def forca_bruta(n, arestas):
    """Corte máximo exato. Fixa o último vértice no grupo 0 (o corte não muda se trocar todos de lado)."""
    arestas = [(u, v, w) for u, v, w in arestas if u != v]  # laço nunca atravessa o corte
    melhor = None
    for mascara in range(1 << (n - 1)):
        corte = 0
        for u, v, w in arestas:
            if ((mascara >> u) & 1) != ((mascara >> v) & 1):
                corte += w
        if melhor is None or corte > melhor:
            melhor = corte
    return melhor


def texto_grafo(n, arestas, fim="\n", linha_fim="\n"):
    """Monta o arquivo de entrada (vértices numerados de 1 a n). u, v vêm numerados de 0."""
    linhas = [f"{n} {len(arestas)}"] + [f"{u + 1} {v + 1} {w}" for u, v, w in arestas]
    return linha_fim.join(linhas) + fim


class Contexto:
    def __init__(self, maxcut, verificador):
        self.maxcut = maxcut
        self.verificador = verificador
        self.tmp = Path(tempfile.mkdtemp(prefix="maxcut_testes_"))
        self.contador = 0

    def novo_caminho(self, sufixo):
        self.contador += 1
        return self.tmp / f"{self.contador}{sufixo}"

    def rodar(self, conteudo, args, timeout=120, saida=None):
        """Roda o maxcut sobre 'conteudo' (str ou bytes; None = arquivo inexistente). Devolve um dict."""
        grafo = self.novo_caminho(".txt")
        if conteudo is not None:
            grafo.write_bytes(conteudo if isinstance(conteudo, bytes) else conteudo.encode())
        saida = saida or self.novo_caminho(".out")
        inicio = time.monotonic()
        p = subprocess.run([str(self.maxcut), str(grafo), str(saida)] + args, capture_output=True, text=True,
                           timeout=timeout)
        r = {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr, "tempo": time.monotonic() - inicio,
             "grafo": grafo, "saida": saida, "corte": None, "verif": None}
        if saida.exists():
            v = subprocess.run([str(self.verificador), str(grafo), str(saida)], capture_output=True, text=True)
            if v.returncode == 0 and re.fullmatch(r"-?\d+", v.stdout.strip()):
                r["corte"] = int(v.stdout.strip())
            else:
                r["verif"] = v.stdout.strip() or v.stderr.strip()
        return r


class Falha(Exception):
    pass


def exigir(cond, msg):
    if not cond:
        raise Falha(msg)


# ------------------------------------------------------- verificações padrão
def checar_sanitizer(r):
    """Binário compilado com -fsanitize: qualquer relatório de ASan/UBSan é falha, mesmo com código 0."""
    exigir("runtime error" not in r["stderr"] and "Sanitizer" not in r["stderr"],
           f"sanitizer reportou: {r['stderr'].strip()[:300]}")


def checar_ok(r, otimo=None, n=None):
    """Execução que deve funcionar: código 0, sem aviso, solução válida e (se dado) corte igual ao ótimo."""
    checar_sanitizer(r)
    exigir(r["rc"] == 0, f"código de saída {r['rc']} (stderr: {r['stderr'].strip()[:150]})")
    exigir("AVISO" not in r["stderr"], f"auto-verificação falhou: {r['stderr'].strip()[:150]}")
    exigir(r["verif"] is None, f"solução inválida segundo o verificador: {r['verif']}")
    exigir(r["corte"] is not None, "o programa não gravou a solução")
    texto = r["saida"].read_bytes()
    exigir(re.fullmatch(rb"[01]+\n", texto) is not None, "saída não é uma única linha de 0/1 terminada em \\n")
    if n is not None:
        exigir(len(texto) == n + 1, f"saída tem {len(texto) - 1} caracteres, esperado {n}")
    m = re.search(r"apos descida gulosa=(-?\d+)", r["stdout"])
    exigir(m is not None, "o programa não imprimiu o corte final")
    exigir(int(m.group(1)) == r["corte"], f"o programa diz {m.group(1)}, o verificador calcula {r['corte']}")
    if otimo is not None:
        exigir(r["corte"] == otimo, f"corte {r['corte']} != ótimo {otimo}")


def checar_erro(r, trecho=None):
    """Execução que deve falhar de forma limpa: código 1, mensagem 'erro:' e nenhuma falha/sinal."""
    checar_sanitizer(r)
    exigir(r["rc"] >= 0, f"o programa foi morto por sinal {-r['rc']} (falha de segmentação?)")
    exigir(r["rc"] == 1, f"código de saída {r['rc']}, esperado 1")
    exigir("erro" in r["stderr"].lower() or "uso:" in r["stderr"].lower(), f"sem mensagem de erro (stderr: {r['stderr'][:100]!r})")
    if trecho:
        exigir(trecho.lower() in r["stderr"].lower(), f"mensagem não menciona '{trecho}': {r['stderr'].strip()[:150]}")


# ------------------------------------------------------------------- os testes
# testes cujo resultado depende da duração: rodam sozinhos, depois do lote paralelo
SERIAIS = ("--tempo 1 (", "--tempo 3:", "estrela")

PEQUENO ="3 2\n1 2 1\n2 3 -1\n"          # grafo válido mínimo para testar argumentos
VAR = ["--varreduras", "3000", "--seed", "1"]


def montar_testes(ctx):
    T = []  # (nome, função)

    def ok(nome, grafo, otimo=None, n=None, args=VAR):
        T.append((nome, lambda: checar_ok(ctx.rodar(grafo, args), otimo, n)))

    def erro(nome, grafo, args=VAR, trecho=None):
        # limite de 30 s: um erro de argumento que o programa aceitasse em silêncio rodaria os 80 s do padrão
        T.append((nome, lambda: checar_erro(ctx.rodar(grafo, args, timeout=30), trecho)))

    # ---- entradas válidas de borda
    ok("n=1, sem arestas", "1 0\n", otimo=0, n=1)
    ok("n=2, uma aresta +1 (ótimo 1)", "2 1\n1 2 1\n", otimo=1, n=2)
    ok("n=2, uma aresta -1 (ótimo 0: mesmo grupo)", "2 1\n1 2 -1\n", otimo=0, n=2)
    ok("n=5, sem arestas (só vértices isolados)", "5 0\n", otimo=0, n=5)
    ok("arquivo sem \\n final", "3 2\n1 2 1\n2 3 -1", otimo=forca_bruta(3, [(0, 1, 1), (1, 2, -1)]), n=3)
    ok("fim de linha CRLF (Windows)", "3 2\r\n1 2 1\r\n2 3 -1\r\n", otimo=1, n=3)
    ok("espaços, tabs e linhas em branco extras", "\n  3   2 \n\n1\t2   1\n\n  2 3 -1  \n\n", otimo=1, n=3)
    ok("vértices em ordem invertida (u > v) e embaralhada", "4 3\n4 1 1\n3 2 -1\n2 4 1\n",
       otimo=forca_bruta(4, [(3, 0, 1), (2, 1, -1), (1, 3, 1)]), n=4)
    ok("sinal '+' explícito no peso", "2 1\n1 2 +1\n", otimo=1, n=2)
    ok("arestas repetidas (somam os pesos) e laços (ignorados)",
       "4 5\n1 2 1\n1 2 1\n2 2 1\n3 4 -1\n3 4 1\n",
       otimo=forca_bruta(4, [(0, 1, 1), (0, 1, 1), (1, 1, 1), (2, 3, -1), (2, 3, 1)]), n=4)
    ok("só laços", "3 2\n1 1 1\n2 2 -1\n", otimo=0, n=3)

    rng = random.Random(2026)
    for sinal, nome in ((1, "+1"), (-1, "-1")):
        arestas = [(u, v, sinal) for u in range(12) for v in range(u + 1, 12) if rng.random() < 0.4]
        ok(f"12 vértices, todas as arestas {nome}", texto_grafo(12, arestas), otimo=forca_bruta(12, arestas), n=12)

    # ---- grafos pequenos aleatórios: tem de achar o ótimo exato
    def aleatorios():
        r = random.Random(12345)
        falhas = []
        for k in range(60):
            n = r.randint(3, 14)
            pares = [(u, v) for u in range(n) for v in range(u + 1, n)]
            arestas = [(u, v, r.choice((1, -1))) for u, v in r.sample(pares, r.randint(0, min(len(pares), 3 * n)))]
            res = ctx.rodar(texto_grafo(n, arestas), ["--varreduras", "2000", "--seed", str(k + 1)])
            try:
                checar_ok(res, forca_bruta(n, arestas), n)
            except Falha as e:
                falhas.append(f"grafo {k} (n={n}, m={len(arestas)}): {e}")
        if falhas:
            raise Falha(f"{len(falhas)} de 60 falharam; ex.: {falhas[0]}")
    T.append(("60 grafos aleatórios pequenos: corte == ótimo por força bruta", aleatorios))

    # ---- grafo estrela: um vértice com 19.999 vizinhos (tabela de limiares com 20.000 entradas)
    def estrela():
        r = random.Random(7)
        n = 20000
        arestas = [(0, v, r.choice((1, -1))) for v in range(1, n)]
        otimo = sum(1 for _, _, w in arestas if w == 1)   # centro de um lado, cada folha escolhe o seu
        res = ctx.rodar(texto_grafo(n, arestas), ["--tempo", "8", "--seed", "1"])
        checar_ok(res, otimo, n)
    T.append(("estrela com 20.000 vértices (grau máximo 19.999): acha o ótimo em 8 s", estrela))

    # ---- tempo
    def tempo_menor_que_a_reserva():
        r = ctx.rodar(PEQUENO, ["--tempo", "1"])
        checar_ok(r)
        exigir(r["tempo"] < 3.0, f"demorou {r['tempo']:.1f}s com --tempo 1")
    T.append(("--tempo 1 (menor que a reserva final): saída válida e rápida", tempo_menor_que_a_reserva))

    def tempo_curto():
        r = ctx.rodar("3 2\n1 2 1\n2 3 -1\n", ["--tempo", "3"])
        checar_ok(r, otimo=1, n=3)
        exigir(0.5 < r["tempo"] < 2.0, f"--tempo 3 deveria durar ~1 s de SA, durou {r['tempo']:.2f}s")
    T.append(("--tempo 3: termina em ~1 s (tempo - reserva)", tempo_curto))

    ok("--varreduras 1 (uma única varredura)", PEQUENO, args=["--varreduras", "1", "--seed", "1"])
    ok("T0 == T1 (temperatura constante)", PEQUENO, args=VAR + ["--t0", "0.5", "--t1", "0.5"])
    ok("mesma semente => mesma solução", PEQUENO, args=VAR)

    def determinismo():
        g = texto_grafo(30, [(u, v, 1 if (u * 7 + v) % 3 else -1) for u in range(30) for v in range(u + 1, 30) if (u + v) % 4 == 0])
        a = ctx.rodar(g, VAR)
        b = ctx.rodar(g, VAR)
        exigir(a["saida"].read_bytes() == b["saida"].read_bytes(), "mesma semente deu soluções diferentes")
    T.append(("determinismo: mesma semente, mesma solução (--varreduras)", determinismo))

    # ---- varredura colorida (OpenMP): o resultado NÃO pode depender do número de threads
    COL = ["--varredura", "colorida", "--varreduras", "300", "--seed", "1"]

    def colorida_borda():
        for nome, grafo, n in (("n=1", "1 0\n", 1), ("só isolados", "6 0\n", 6), ("n=2", "2 1\n1 2 1\n", 2)):
            checar_ok(ctx.rodar(grafo, COL + ["--threads", "4"]), n=n)
    T.append(("colorida: n=1, só vértices isolados e n=2 (4 threads)", colorida_borda))

    def colorida_otimo():
        r = random.Random(555)
        falhas = []
        for k in range(30):
            n = r.randint(3, 14)
            pares = [(u, v) for u in range(n) for v in range(u + 1, n)]
            arestas = [(u, v, r.choice((1, -1))) for u, v in r.sample(pares, r.randint(0, min(len(pares), 3 * n)))]
            res = ctx.rodar(texto_grafo(n, arestas), ["--varredura", "colorida", "--threads", "3", "--varreduras", "2000", "--seed", str(k + 1)])
            try:
                checar_ok(res, forca_bruta(n, arestas), n)
            except Falha as e:
                falhas.append(f"grafo {k} (n={n}): {e}")
        if falhas:
            raise Falha(f"{len(falhas)} de 30 falharam; ex.: {falhas[0]}")
    T.append(("colorida (3 threads): 30 grafos pequenos == ótimo por força bruta", colorida_otimo))

    def colorida_mesma_solucao_com_qualquer_numero_de_threads():
        r = random.Random(99)
        n = 800
        pares = set()
        while len(pares) < 4000:
            u, v = r.randrange(n), r.randrange(n)
            if u != v:
                pares.add((min(u, v), max(u, v)))
        aleatorio = texto_grafo(n, [(u, v, r.choice((1, -1))) for u, v in sorted(pares)])
        # hub: o vértice 1 liga em todos, e há arestas extras entre as folhas ⇒ muitos vizinhos em comum
        # (vários vértices da mesma cor atualizam o mesmo delta[j] ao mesmo tempo: estressa o atomic)
        extras = {(min(u, v), max(u, v)) for u, v in ((r.randrange(1, 1500), r.randrange(1, 1500)) for _ in range(6000)) if u != v}
        arestas = [(0, v, r.choice((1, -1))) for v in range(1, 1500)] + [(u, v, r.choice((1, -1))) for u, v in sorted(extras)]
        hub = texto_grafo(1500, arestas)
        for nome, grafo in (("aleatório n=800", aleatorio), ("hub n=1500", hub)):
            saidas = {}
            for th in (1, 2, 4, 8):
                res = ctx.rodar(grafo, COL + ["--threads", str(th)])
                checar_ok(res)
                saidas[th] = res["saida"].read_bytes()
            exigir(len(set(saidas.values())) == 1,
                   f"{nome}: a solução muda com o número de threads (indício de corrida): "
                   + ", ".join(f"{th}t={hash(s) % 10**6}" for th, s in saidas.items()))
    T.append(("colorida: mesma solução com 1, 2, 4 e 8 threads (aleatório e hub)", colorida_mesma_solucao_com_qualquer_numero_de_threads))

    ok("--varredura colorida com 1 thread", PEQUENO, args=["--varredura", "colorida", "--threads", "1", "--varreduras", "100"])
    ok("--threads 4 sem --varredura usa a colorida", PEQUENO, args=["--threads", "4", "--varreduras", "100"])
    ok("--threads 0 (todas as CPUs disponíveis)", PEQUENO, args=["--threads", "0", "--varreduras", "100"])
    ok("--threads 100 (acima do número de CPUs: é reduzido, não falha)", PEQUENO, args=["--threads", "100", "--varreduras", "100"])
    ok("--varredura sequencial com 1 thread", PEQUENO, args=["--varredura", "sequencial", "--varreduras", "100"])

    # ---- entradas inválidas (devem dar erro limpo)
    erro("cabeçalho inválido", "abc def\n", trecho="cabeçalho")
    erro("arquivo vazio", "", trecho="cabeçalho")
    erro("n = 0", "0 0\n", trecho="limites")
    erro("n negativo", "-3 0\n", trecho="limites")
    erro("m negativo", "3 -1\n", trecho="limites")
    erro("faltam arestas (m maior que as linhas)", "3 3\n1 2 1\n", trecho="ausente")
    erro("vértice 0 (os vértices vão de 1 a n)", "3 1\n0 2 1\n", trecho="fora de")
    erro("vértice maior que n", "3 1\n1 4 1\n", trecho="fora de")
    erro("peso 5", "3 1\n1 2 5\n", trecho="peso")
    erro("peso 0", "3 1\n1 2 0\n", trecho="peso")
    erro("lixo no lugar de um número", "3 1\n1 2 x\n", trecho="aresta")
    erro("peso decimal (1.0)", "3 1\n1 2 1.0\n", trecho="aresta")
    erro("número gigante no cabeçalho", "99999999999999999999 1\n1 2 1\n")
    erro("m grande demais para índices de 32 bits", "10 2000000000\n", trecho="grande")

    def inexistente():
        checar_erro(ctx.rodar(None, VAR), "abrir")
    T.append(("arquivo de entrada inexistente", inexistente))

    def saida_em_pasta_inexistente():
        checar_erro(ctx.rodar(PEQUENO, VAR, saida=Path("/nao/existe/dir/saida.txt")), "criar")
    T.append(("saída em pasta inexistente", saida_em_pasta_inexistente))

    def sem_argumentos():
        p = subprocess.run([str(ctx.maxcut)], capture_output=True, text=True)
        checar_erro({"rc": p.returncode, "stderr": p.stderr}, "uso")
        p = subprocess.run([str(ctx.maxcut), "so_um_argumento.txt"], capture_output=True, text=True)
        checar_erro({"rc": p.returncode, "stderr": p.stderr}, "uso")
    T.append(("sem argumentos / só um argumento", sem_argumentos))

    # ---- argumentos inválidos
    for nome, args, trecho in [
        ("opção desconhecida", ["--foo", "1"], "desconhecido"),
        ("opção sem valor (--seed)", ["--seed"], "faltou"),
        ("--seed não numérico", ["--seed", "abc"], "--seed"),
        ("--seed negativo", ["--seed", "-5"], "--seed"),
        ("--t0 não numérico", ["--t0", "abc"], "--t0"),
        ("--t1 zero", ["--t1", "0"], "temperaturas"),
        ("--t1 maior que --t0", ["--t0", "1", "--t1", "2"], "temperaturas"),
        ("--t0 NaN", ["--t0", "nan"], "temperaturas"),
        ("--tempo zero", ["--tempo", "0"], "tempo"),
        ("--tempo negativo", ["--tempo", "-4"], "tempo"),
        ("--tempo NaN", ["--tempo", "nan"], "tempo"),
        ("--tempo infinito", ["--tempo", "inf"], "tempo"),
        ("--tempo absurdo (1e30)", ["--tempo", "1e30"], "tempo"),
        ("--tempo e --varreduras juntos", ["--tempo", "5", "--varreduras", "10"], "juntos"),
        ("--varreduras zero", ["--varreduras", "0"], "varreduras"),
        ("--varreduras negativo", ["--varreduras", "-3"], "varreduras"),
        ("--varreduras não numérico", ["--varreduras", "dez"], "--varreduras"),
        ("--trace em pasta inexistente", ["--varreduras", "10", "--trace", "/nao/existe/t.csv"], "criar"),
        ("--threads negativo", ["--varreduras", "10", "--threads", "-2"], "threads"),
        ("--threads absurdo (5000)", ["--varreduras", "10", "--threads", "5000"], "threads"),
        ("--threads não numérico", ["--varreduras", "10", "--threads", "muitas"], "--threads"),
        ("--varredura desconhecida", ["--varreduras", "10", "--varredura", "paralela"], "--varredura"),
        ("--varredura sequencial com --threads > 1", ["--varreduras", "10", "--varredura", "sequencial", "--threads", "4"], "sequencial"),
    ]:
        erro(f"argumento inválido: {nome}", PEQUENO, args=args, trecho=trecho)

    return T


def principal():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--maxcut", type=Path, default=RAIZ / "maxcut")
    ap.add_argument("--verificador", type=Path, default=RAIZ / "build" / "verificador")
    ap.add_argument("--filtro", help="roda só os testes cujo nome contém este texto")
    ap.add_argument("--paralelo", type=int, default=min(8, os.cpu_count() or 1),
                    help="testes simultâneos (threads; padrão min(8, nº de CPUs)); 1 = tudo em sequência")
    cfg = ap.parse_args()

    for caminho in (cfg.maxcut, cfg.verificador):
        if not caminho.exists():
            sys.exit(f"erro: {caminho} não existe (rode 'make testes', que compila os dois)")

    ctx = Contexto(cfg.maxcut, cfg.verificador)
    testes = [(n, f) for n, f in montar_testes(ctx) if not cfg.filtro or cfg.filtro in n]

    def executar(item):
        nome, f = item
        inicio = time.monotonic()
        try:
            f()
            erro = None
        except Falha as e:
            erro = str(e)
        except subprocess.TimeoutExpired:
            erro = "estourou o tempo limite do teste"
        except Exception as e:  # bug no próprio teste ou no ambiente: não derruba a bateria
            erro = f"exceção inesperada: {type(e).__name__}: {e}"
        return nome, erro, time.monotonic() - inicio

    # Os testes sensíveis a tempo rodam sozinhos, depois do lote paralelo (a carga distorceria a duração medida).
    paralelos = [t for t in testes if not t[0].startswith(SERIAIS)]
    seriais = [t for t in testes if t[0].startswith(SERIAIS)]

    inicio_total = time.monotonic()
    resultados = {}
    with ThreadPoolExecutor(max_workers=max(1, cfg.paralelo)) as pool:
        for nome, erro, dur in pool.map(executar, paralelos):
            resultados[nome] = (erro, dur)
    for item in seriais:
        nome, erro, dur = executar(item)
        resultados[nome] = (erro, dur)

    falhas = 0
    for nome, _ in testes:  # imprime na ordem original
        erro, dur = resultados[nome]
        if erro is None:
            print(f"[ok  ] {nome}  ({dur:.1f}s)")
        else:
            falhas += 1
            print(f"[FALHA] {nome}  ({dur:.1f}s)\n         -> {erro}")

    print(f"\n{len(testes) - falhas} de {len(testes)} testes passaram "
          f"({time.monotonic() - inicio_total:.0f}s, {max(1, cfg.paralelo)} em paralelo).")
    sys.exit(1 if falhas else 0)


if __name__ == "__main__":
    principal()
