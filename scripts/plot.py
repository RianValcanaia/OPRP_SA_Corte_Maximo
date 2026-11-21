#!/usr/bin/env python3
"""Gráficos dos experimentos do TF de Max-Cut. Precisa do matplotlib (scripts/requirements.txt).

    python3 -m venv .venv && .venv/bin/pip install -r scripts/requirements.txt
    .venv/bin/python scripts/plot.py convergencia results/traces/baseline-sequencial_n100000_s1_s1.csv
    .venv/bin/python scripts/plot.py resultados results/baseline-sequencial.csv results/outro.csv

convergencia  trajetória de uma execução (gerada com --trace): corte atual e melhor × tempo, e a
              temperatura (escala log). O painel de baixo amplia a parte final.
resultados    cortes de cada execução por instância, um ponto por execução, a mediana (traço) e a
              meta do aux/README.md (linha tracejada). Cada CSV vira um grupo de pontos.
escalabilidade  speedup e eficiência × nº de threads, a partir de results/escalabilidade-openmp.csv
              (colunas maquina,instancia,threads,varreduras_por_s). A referência é a versão sequencial (1 thread).
"""
import argparse
import csv
import statistics
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rodar import METAS  # noqa: E402  (as mesmas metas do rodar.py)


def importar_matplotlib():
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except ImportError:
        sys.exit("erro: matplotlib não está instalado. Instale com:\n"
                 "  python3 -m venv .venv && .venv/bin/pip install -r scripts/requirements.txt\n"
                 "e rode com .venv/bin/python scripts/plot.py ...")


def ler_traco(caminho):
    with open(caminho, newline="") as f:
        linhas = list(csv.DictReader(f))
    if not linhas:
        sys.exit(f"erro: trace vazio: {caminho}")
    col = lambda nome: [float(l[nome]) for l in linhas]
    return col("segundos"), col("temperatura"), col("corte"), col("melhor")


def convergencia(args):
    plt = importar_matplotlib()
    t, temp, corte, melhor = ler_traco(args.trace)
    nome = Path(args.trace).stem

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))

    # painel de cima: trajetória inteira
    ax1.plot(t, corte, color="tab:blue", lw=0.8, label="corte atual")
    ax1.plot(t, melhor, color="tab:red", lw=1.0, label="melhor visto")
    ax1.set_ylabel("corte")
    ax1.set_xlabel("tempo no SA (s)")
    ax1.set_title(f"Convergência — {nome}")
    ax1.grid(alpha=0.3)
    eixo_t = ax1.twinx()
    eixo_t.plot(t, temp, color="tab:green", lw=0.8, ls="--", label="temperatura")
    eixo_t.set_yscale("log")
    eixo_t.set_ylabel("temperatura (escala log)")
    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = eixo_t.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="lower right")

    # painel de baixo: o final (últimos 25% do tempo), onde está a diferença entre as execuções
    corte_t = t[0] + 0.75 * (t[-1] - t[0])
    idx = [i for i, x in enumerate(t) if x >= corte_t] or list(range(len(t)))
    ax2.plot([t[i] for i in idx], [corte[i] for i in idx], color="tab:blue", lw=0.8, label="corte atual")
    ax2.plot([t[i] for i in idx], [melhor[i] for i in idx], color="tab:red", lw=1.0, label="melhor visto")
    ax2.axhline(melhor[-1], color="k", lw=0.6, ls=":", label=f"resultado final = {melhor[-1]:.0f}")
    ax2.set_xlabel("tempo no SA (s)")
    ax2.set_ylabel("corte")
    ax2.set_title("ampliação: últimos 25 % do tempo (a última linha é o resultado após a descida gulosa)")
    ax2.grid(alpha=0.3)
    ax2.legend(loc="lower right")

    fig.tight_layout()
    saida = Path(args.saida) if args.saida else RAIZ / "results" / f"{nome}.png"
    saida.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(saida, dpi=130)
    print(f"gravado: {saida}")


def resultados(args):
    plt = importar_matplotlib()

    # grupos: um por CSV (usa o rótulo da primeira linha), instância -> lista de cortes
    grupos = []
    for caminho in args.csv:
        with open(caminho, newline="") as f:
            linhas = list(csv.DictReader(f))
        if not linhas:
            continue
        por_inst = {}
        for l in linhas:
            if l["corte"] != "":
                por_inst.setdefault(l["instancia"], []).append(int(l["corte"]))
        grupos.append((linhas[0]["rotulo"], por_inst))
    if not grupos:
        sys.exit("erro: nenhum resultado nos CSVs")

    instancias = sorted({i for _, d in grupos for i in d}, key=lambda s: (len(s), s))
    fig, eixos = plt.subplots(1, len(instancias), figsize=(4.2 * len(instancias), 5), squeeze=False)

    for ax, inst in zip(eixos[0], instancias):
        for k, (rotulo, d) in enumerate(grupos):
            cortes = d.get(inst, [])
            if not cortes:
                continue
            xs = [k + (i - (len(cortes) - 1) / 2) * 0.06 for i in range(len(cortes))]
            ax.plot(xs, cortes, "o", color=f"C{k}")
            med = statistics.median(cortes)
            ax.hlines(med, k - 0.25, k + 0.25, color=f"C{k}", lw=2)
            ax.annotate(f"{med:.0f}", (k + 0.27, med), va="center", fontsize=8)
        if inst in METAS:
            ax.axhline(METAS[inst], color="k", ls="--", lw=0.8, label=f"meta {METAS[inst]}")
            ax.legend(loc="lower right", fontsize=8)
        ax.set_xticks(range(len(grupos)))
        ax.set_xticklabels([r for r, _ in grupos], rotation=20, ha="right", fontsize=8)
        ax.set_xlim(-0.6, len(grupos) - 0.4)
        ax.set_title(inst)
        ax.set_ylabel("corte")
        ax.grid(alpha=0.3)

    fig.suptitle("Cortes por execução (traço = mediana, a nota da competição)")
    fig.tight_layout()
    saida = Path(args.saida) if args.saida else RAIZ / "results" / "resultados.png"
    saida.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(saida, dpi=130)
    print(f"gravado: {saida}")


def escalabilidade(args):
    plt = importar_matplotlib()
    dados = {}
    with open(args.csv, newline="", encoding="utf-8") as f:
        for l in csv.DictReader(f):
            dados.setdefault((l["maquina"], l["instancia"]), {})[int(l["threads"])] = float(l["varreduras_por_s"])

    instancias = sorted({i for _, i in dados})
    fig, eixos = plt.subplots(2, len(instancias), figsize=(6 * len(instancias), 8), squeeze=False)
    for col, inst in enumerate(instancias):
        ax_s, ax_e = eixos[0][col], eixos[1][col]
        maximo = 1
        for (maq, i), pts in sorted(dados.items()):
            if i != inst:
                continue
            ts = sorted(pts)
            base = pts[1]
            sp = [pts[t] / base for t in ts]
            maximo = max(maximo, max(ts))
            ax_s.plot(ts, sp, "o-", label=maq)
            ax_e.plot(ts, [x / t for x, t in zip(sp, ts)], "o-", label=maq)
        ax_s.plot([1, maximo], [1, maximo], "k:", label="ideal")
        ax_s.set_title(f"{inst}: speedup (vs sequencial)")
        ax_e.set_title(f"{inst}: eficiência (speedup / threads)")
        ax_s.set_ylabel("speedup")
        ax_e.set_ylabel("eficiência")
        for ax in (ax_s, ax_e):
            ax.set_xlabel("threads")
            ax.grid(alpha=0.3)
            ax.legend()
    fig.tight_layout()
    saida = Path(args.saida) if args.saida else RAIZ / "results" / "escalabilidade-openmp.png"
    fig.savefig(saida, dpi=130)
    print(f"gravado: {saida}")


def principal():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("convergencia", help="trajetória de uma execução (CSV gerado com --trace)")
    c.add_argument("trace")
    c.add_argument("--saida", help="PNG de saída (padrão results/<nome do trace>.png)")
    c.set_defaults(f=convergencia)

    r = sub.add_parser("resultados", help="cortes por execução, por instância")
    r.add_argument("csv", nargs="+", help="um ou mais CSVs gerados pelo rodar.py (um grupo de pontos por CSV)")
    r.add_argument("--saida", help="PNG de saída (padrão results/resultados.png)")
    r.set_defaults(f=resultados)

    e = sub.add_parser("escalabilidade", help="speedup e eficiência × threads")
    e.add_argument("csv", nargs="?", default=str(RAIZ / "results" / "escalabilidade-openmp.csv"))
    e.add_argument("--saida", help="PNG de saída (padrão results/escalabilidade-openmp.png)")
    e.set_defaults(f=escalabilidade)

    args = ap.parse_args()
    args.f(args)


if __name__ == "__main__":
    principal()
