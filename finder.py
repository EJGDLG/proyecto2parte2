# finder.py
import itertools
import queue
import threading
import subprocess
import sys
import random
import os
from typing import List

ALPHABET = "abcdefghijklmnopqrstuvwxyz0123456789"

def gen_suffixes(length: int) -> List[str]:
    return [''.join(p) for p in itertools.product(ALPHABET, repeat=length)]

def ping_host(host: str, timeout_ms: int = 800) -> bool:
    # Windows vs Unix
    if os.name == "nt":
        # -n 1: un eco; -w timeout ms
        cmd = ["ping", "-n", "1", "-w", str(timeout_ms), host]
    else:
        # -c 1: un eco; -W timeout s (redondeo hacia arriba)
        sec = max(1, int(round(timeout_ms / 1000.0)))
        cmd = ["ping", "-c", "1", "-W", str(sec), host]
    try:
        return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    except Exception:
        return False

def worker_dynamic(base: str, q: queue.Queue, found: list, lock: threading.Lock):
    while True:
        item = q.get()
        if item is None:  # señal de fin
            q.task_done()
            break
        host = base + item
        if ping_host(host):
            # guardar hallazgo
            with lock:
                found.append(item)
        q.task_done()

def worker_static(base: str, items: List[str], found: list, lock: threading.Lock):
    for suf in items:
        if ping_host(base + suf):
            with lock:
                found.append(suf)

def run_search(base: str, length: int, n_workers: int = 8, variant: str = "dynamic"):
    """
    variant:
      - 'dynamic': cola compartida (aleatorio opcional)
      - 'round_robin': partición secuencial por bloques
      - 'random_static': baraja la lista y reparte por bloques
    """
    suffixes = gen_suffixes(length)
    found = []
    lock = threading.Lock()

    if variant == "dynamic":
        # Cola compartida: excelente para balanceo. También podemos barajar.
        random.shuffle(suffixes)
        q = queue.Queue(maxsize=4 * n_workers)
        threads = []
        for _ in range(n_workers):
            t = threading.Thread(target=worker_dynamic, args=(base, q, found, lock), daemon=True)
            t.start()
            threads.append(t)
        for s in suffixes:
            q.put(s)
        for _ in range(n_workers):
            q.put(None)
        q.join()
        for t in threads:
            t.join()

    else:
        # Partición estática
        if variant == "random_static":
            random.shuffle(suffixes)

        # dividir en bloques casi iguales
        chunk = (len(suffixes) + n_workers - 1) // n_workers
        threads = []
        for i in range(n_workers):
            start = i * chunk
            end = min(len(suffixes), (i + 1) * chunk)
            if start >= end:
                break
            t = threading.Thread(
                target=worker_static,
                args=(base, suffixes[start:end], found, lock),
                daemon=True
            )
            t.start()
            threads.append(t)
        for t in threads:
            t.join()

    return found

if __name__ == "__main__":
    # Ejemplos:
    # python finder.py base- 1 dynamic
    # python finder.py host- 2 random_static
    if len(sys.argv) < 4:
        print("Uso: python finder.py <base> <length> <variant> [n_workers]")
        print("variant ∈ {dynamic, round_robin, random_static} (round_robin == partición secuencial)")
        sys.exit(1)

    base = sys.argv[1]        # p.ej. "srv-"
    length = int(sys.argv[2]) # longitud del sufijo
    variant = sys.argv[3]     # dynamic | round_robin | random_static
    n_workers = int(sys.argv[4]) if len(sys.argv) > 4 else os.cpu_count() or 4

    hits = run_search(base, length, n_workers=n_workers, variant=variant)
    if hits:
        print("Encontrados (sufijos con ping OK):", ", ".join(hits))
    else:
        print("No se encontraron hosts válidos.")
