#!/usr/bin/env python3
"""Roda o maxcut várias vezes por instância, confere cada saída com o verificador
externo e resume os cortes (a nota da competição é a MEDIANA de 3 execuções).

Só usa a biblioteca padrão do Python.

Exemplos:
    # baseline oficial: 3 execuções (sementes 1,2,3) de cada instância, 80 s cada
    python3 scripts/rodar.py --rotulo baseline-sequencial --trace

    # teste rápido
    python3 scripts/rodar.py --instancias n1000_s1,n20000_s1 --execucoes 2 --tempo 5

    # experimento com parâmetros diferentes (repassados ao maxcut)
    python3 scripts/rodar.py --rotulo t0-3 --extra "--t0 3.0 --t1 0.15" --tempo 20

Cada execução vira uma linha no CSV (results/<rotulo>.csv, acrescentando se já existir).
Com --trace, a trajetória de cada execução vai para results/traces/ (use scripts/plot.py).
"""
import argparse
import csv
import datetime
import os
import re
import shlex
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent

# Melhores cortes conhecidos (aux/README.md): SA sequencial de referência,
# 10.000 varreduras, melhor de 3 sementes. Não são ótimos provados e podem ser superados.
METAS = {
    "exemplo_n5": 3,
    "n20_s1": 18,
    "n100_s1": 118,
    "n1000_s1": 1178,
    "n20000_s1": 23668,
    "n100000_s1": 116548,
}

COLUNAS = ["rotulo", "data", "git", "maquina", "cpus", "instancia", "seed", "modo", "tempo_alvo_s",
           "tempo_externo_s", "corte", "varreduras", "t0", "t1", "status"]


# ---------------------------------------------------------------- compilação
def mais_novo_que(alvo, fontes):
    """True se 'alvo' existe e é mais novo que todas as fontes."""
    if not alvo.exists():
        return False
    return all(alvo.stat().st_mtime >= f.stat().st_mtime for f in fontes)


def garantir_binarios(maxcut, verificador, compilar):
    """Compila o maxcut (se faltar ou estiver desatualizado) e o verificador (se faltar)."""
    (RAIZ / "build").mkdir(exist_ok=True)

    if not mais_novo_que(verificador, [RAIZ / "aux" / "verificador.c"]):
        print(f"compilando {verificador.relative_to(RAIZ)} ...")
        subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-o", str(verificador), str(RAIZ / "aux" / "verificador.c")],
                       check=True)

    fontes = sorted((RAIZ / "src").glob("*.cpp")) + sorted((RAIZ / "src").glob("*.hpp"))
    if compilar and (RAIZ / "Makefile").exists() and maxcut == RAIZ / "maxcut":
        # usa o Makefile do projeto (as mesmas flags da entrega); o make só recompila o que mudou
        subprocess.run(["make", "-s", "-C", str(RAIZ), "maxcut"], check=True)
    elif compilar and not mais_novo_que(maxcut, fontes):
        print(f"compilando {maxcut.relative_to(RAIZ)} ...")
        cpps = [str(f) for f in fontes if f.suffix == ".cpp"]
        subprocess.run(["g++", "-std=c++17", "-O3", "-Wall", "-Wextra", "-o", str(maxcut)] + cpps, check=True)
    elif not maxcut.exists():
        sys.exit(f"erro: {maxcut} não existe (tire o --sem-compilar ou compile antes)")


def versao_git():
    """Hash curto do commit (com '+mod' se src/ tem alterações não commitadas), ou '' fora de um repositório."""
    try:
        h = subprocess.run(["git", "-C", str(RAIZ), "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
        if h.returncode != 0:
            return ""
        sujo = subprocess.run(["git", "-C", str(RAIZ), "status", "--porcelain", "src"], capture_output=True, text=True)
        return h.stdout.strip() + ("+mod" if sujo.stdout.strip() else "")
    except FileNotFoundError:
        return ""


# ------------------------------------------------------------------ execução
def rodar_uma(cfg, instancia, seed):
    """Uma execução do maxcut + conferência pelo verificador. Devolve um dict com o resultado."""
    grafo = RAIZ / "entradas" / f"{instancia}.txt"
    if not grafo.exists():
        sys.exit(f"erro: instância não encontrada: {grafo}")

    with tempfile.TemporaryDirectory() as tmp:
        saida = Path(tmp) / "solucao.txt"
        cmd = [str(cfg.maxcut), str(grafo), str(saida), "--seed", str(seed)]
        cmd += ["--varreduras", str(cfg.varreduras)] if cfg.varreduras else ["--tempo", str(cfg.tempo)]
        cmd += shlex.split(cfg.extra)
        if cfg.trace:
            traco = RAIZ / "results" / "traces" / f"{cfg.rotulo}_{instancia}_s{seed}.csv"
            traco.parent.mkdir(parents=True, exist_ok=True)
            cmd += ["--trace", str(traco)]

        linha = {"rotulo": cfg.rotulo, "data": datetime.datetime.now().isoformat(timespec="seconds"), "git": cfg.git,
                 "maquina": socket.gethostname(), "cpus": os.cpu_count(), "instancia": instancia, "seed": seed,
                 "modo": f"varreduras={cfg.varreduras}" if cfg.varreduras else "tempo",
                 "tempo_alvo_s": "" if cfg.varreduras else cfg.tempo, "tempo_externo_s": "", "corte": "",
                 "varreduras": "", "t0": "", "t1": "", "status": "ok"}

        inicio = time.monotonic()
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=cfg.limite)
        except subprocess.TimeoutExpired:
            linha["tempo_externo_s"] = round(time.monotonic() - inicio, 2)
            linha["status"] = f"ESTOUROU o limite de {cfg.limite:g}s (nota zero)"
            return linha
        linha["tempo_externo_s"] = round(time.monotonic() - inicio, 2)

        saida_texto = p.stdout
        m = re.search(r"\| varreduras=(\d+)", saida_texto)
        if m:
            linha["varreduras"] = int(m.group(1))
        m = re.search(r"T0=(\S+) T1=(\S+)", saida_texto)
        if m:
            linha["t0"], linha["t1"] = m.group(1), m.group(2)
        reportado = re.search(r"apos descida gulosa=(-?\d+)", saida_texto)

        if p.returncode != 0:
            linha["status"] = "ERRO: " + (p.stderr.strip().splitlines() or ["código " + str(p.returncode)])[0]
            return linha

        v = subprocess.run([str(cfg.verificador), str(grafo), str(saida)], capture_output=True, text=True)
        if v.returncode != 0 or not re.fullmatch(r"-?\d+", v.stdout.strip()):
            linha["status"] = "INVALIDA: " + (v.stdout.strip() or v.stderr.strip())
            return linha
        linha["corte"] = int(v.stdout.strip())

        avisos = []
        if "AVISO" in p.stderr:
            avisos.append("AVISO de auto-verificação")
        if reportado and int(reportado.group(1)) != linha["corte"]:
            avisos.append(f"DIVERGE (programa disse {reportado.group(1)})")
        if linha["tempo_externo_s"] > cfg.limite:
            avisos.append(f"passou de {cfg.limite:g}s")
        if avisos:
            linha["status"] = "; ".join(avisos)
        return linha


# -------------------------------------------------------------------- resumo
def resumir(linhas, instancias):
    """Tabela de texto: por instância, mediana/min/max/média dos cortes válidos, meta e tempo externo máximo."""
    cab = f"{'instância':<12} {'exec':>4} {'mediana':>9} {'min':>9} {'max':>9} {'média':>10} {'meta':>9} {'med-meta':>9} {'t_ext máx':>9}  problemas"
    out = [cab, "-" * len(cab)]
    for inst in instancias:
        do_inst = [l for l in linhas if l["instancia"] == inst]
        # 'corte' só é preenchido quando o verificador aceitou a solução
        cortes = [l["corte"] for l in do_inst if l["corte"] != ""]
        tempos = [l["tempo_externo_s"] for l in do_inst if l["tempo_externo_s"] != ""]
        problemas = [f"s{l['seed']}: {l['status']}" for l in do_inst if l["status"] != "ok"]
        meta = METAS.get(inst)
        if cortes:
            med = statistics.median(cortes)
            out.append(f"{inst:<12} {len(do_inst):>4} {med:>9.1f} {min(cortes):>9} {max(cortes):>9} "
                       f"{statistics.mean(cortes):>10.1f} {meta if meta is not None else '-':>9} "
                       f"{(f'{med - meta:+.1f}' if meta is not None else '-'):>9} "
                       f"{max(tempos) if tempos else '-':>9}  {'; '.join(problemas)}")
        else:
            out.append(f"{inst:<12} {len(do_inst):>4} {'(sem cortes válidos)':>40}  {'; '.join(problemas)}")
    return "\n".join(out)


def principal():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--instancias", default="n1000_s1,n20000_s1,n100000_s1",
                    help="nomes (sem .txt) dos arquivos de entradas/, separados por vírgula")
    ap.add_argument("--execucoes", type=int, default=3, help="execuções por instância (padrão 3, como na competição)")
    ap.add_argument("--sementes", help="lista de sementes (padrão 1..execucoes)")
    ap.add_argument("--tempo", type=float, default=80.0, help="--tempo do maxcut, em s (padrão 80, o do programa)")
    ap.add_argument("--varreduras", type=int, help="usa varreduras fixas em vez de tempo (modo determinístico)")
    ap.add_argument("--extra", default="", help='flags extras para o maxcut, ex.: "--t0 3.0 --t1 0.1"')
    ap.add_argument("--rotulo", default="execucao", help="nome do experimento (nome do CSV e dos traces)")
    ap.add_argument("--saida", help="CSV de saída (padrão results/<rotulo>.csv; acrescenta se existir)")
    ap.add_argument("--trace", action="store_true", help="grava o trace de cada execução em results/traces/")
    ap.add_argument("--paralelo", type=int, default=1,
                    help="execuções simultâneas (padrão 1; >1 acelera experimentos mas altera a velocidade medida)")
    ap.add_argument("--limite", type=float, default=90.0, help="limite de tempo externo em s (padrão 90, o da competição)")
    ap.add_argument("--maxcut", type=Path, default=RAIZ / "maxcut")
    ap.add_argument("--verificador", type=Path, default=RAIZ / "build" / "verificador")
    ap.add_argument("--sem-compilar", action="store_true", help="não (re)compila o maxcut; usa --maxcut como está")
    cfg = ap.parse_args()

    instancias = [i.strip() for i in cfg.instancias.split(",") if i.strip()]
    sementes = [int(s) for s in cfg.sementes.split(",")] if cfg.sementes else list(range(1, cfg.execucoes + 1))

    garantir_binarios(cfg.maxcut, cfg.verificador, not cfg.sem_compilar)
    cfg.git = versao_git()
    saida_csv = Path(cfg.saida) if cfg.saida else RAIZ / "results" / f"{cfg.rotulo}.csv"
    saida_csv.parent.mkdir(parents=True, exist_ok=True)

    total = len(instancias) * len(sementes)
    modo = f"{cfg.varreduras} varreduras" if cfg.varreduras else f"{cfg.tempo:g} s"
    print(f"{total} execuções ({modo} cada), rótulo '{cfg.rotulo}', git {cfg.git or '-'}, {cfg.paralelo} em paralelo")
    if not cfg.varreduras and cfg.paralelo == 1:
        print(f"duração estimada: ~{total * cfg.tempo / 60:.1f} min")

    tarefas = [(inst, s) for inst in instancias for s in sementes]
    linhas = []
    with ThreadPoolExecutor(max_workers=max(1, cfg.paralelo)) as pool:
        futuros = [pool.submit(rodar_uma, cfg, inst, s) for inst, s in tarefas]
        for f in futuros:  # na ordem das tarefas (mantém a saída organizada)
            l = f.result()
            linhas.append(l)
            print(f"  {l['instancia']:<12} seed={l['seed']:<3} externo={l['tempo_externo_s']}s corte={l['corte']} "
                  f"varreduras={l['varreduras']}  {'' if l['status'] == 'ok' else l['status']}", flush=True)

    novo = not saida_csv.exists()
    with open(saida_csv, "a", newline="") as arq:
        w = csv.DictWriter(arq, fieldnames=COLUNAS)
        if novo:
            w.writeheader()
        w.writerows(linhas)

    tabela = resumir(linhas, instancias)
    print("\n" + tabela)
    print(f"\nCSV: {saida_csv}")
    saida_csv.with_suffix(".resumo.txt").write_text(
        f"rótulo: {cfg.rotulo} | git: {cfg.git or '-'} | modo: {modo} | máquina: {socket.gethostname()} "
        f"({os.cpu_count()} CPUs) | {datetime.datetime.now().isoformat(timespec='seconds')}\n\n{tabela}\n")

    ruins = [l for l in linhas if l["status"] != "ok"]
    sys.exit(1 if any(("INVALIDA" in l["status"] or "ESTOUROU" in l["status"] or "ERRO" in l["status"]) for l in ruins) else 0)


if __name__ == "__main__":
    principal()
