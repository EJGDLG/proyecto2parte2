README — Finder (búsqueda distribuida por sufijos)

Este proyecto busca un host “perdido” probando ping sobre un prefijo/base concatenado con sufijos generados (a–z, 0–9) de longitud L, utilizando hilos y diferentes estrategias de división de trabajo.

Requisitos

Python 3.8+

Acceso al comando ping:

Windows: ping -n

Linux/macOS: ping -c

Red funcionando (o usa el modo simulado).

Instalación
# Clona o copia finder.py en una carpeta
python --version

Uso básico
python finder.py <base> <length> <variant> [n_workers] [--verbose] [--selftest]
# variant ∈ { dynamic | round_robin | random_static }


base: prefijo, p. ej. srv- o 192.168.1.

length: longitud del sufijo (ej. 1 → a..z,0..9)

variant:

dynamic: cola compartida (mejor balanceo de carga)

round_robin: partición secuencial por bloques

random_static: baraja la lista y luego particiona por bloques

n_workers: hilos (por defecto, núcleos de la máquina)

--verbose: salida detallada

--selftest: corre pruebas integradas (ver más abajo)

Comandos de pruebas + resultados esperados (y por qué)

Nota: en Windows y Linux funcionan igual. Si tu red/host bloquea ICMP, ping podría fallar. Empieza por localhost para validar.

1) Prueba positiva mínima (debe ENCONTRAR)

Comando

python finder.py 127.0.0.1 0 dynamic


Resultado esperado

Encontrados (sufijos con ping OK):
Tiempo total: X.XXXs | workers=... | variant=dynamic


Por qué
length=0 genera el único sufijo vacío "" → se hace ping a 127.0.0.1 directamente, que normalmente responde.

2) Prueba negativa mínima (NO debe encontrar)

Comando

python finder.py 203.0.113.1 0 dynamic


Resultado esperado

No se encontraron hosts válidos.
Tiempo total: X.XXXs | ...


Por qué
203.0.113.0/24 (TEST-NET-3) está reservado para documentación, típicamente no responde.

3) Autotest integrado (2 casos: uno positivo y uno negativo)

Comando

python finder.py --selftest


Resultado esperado

== Selftest 1: 127.0.0.1 (debe ENCONTRAR) ==
Hits: ['']           # o equivalente
OK en X.XXX s

== Selftest 2: 203.0.113.1 (NO debe encontrar) ==
Hits: []
OK en X.XXX s


Por qué
Verifica rápidamente que ping_host y la coordinación de hilos funcionan en tu entorno.

4) Diversas estrategias de división (comparación)
4.a) Dinámica (cola compartida + barajado)
python finder.py 203.0.113.1 2 dynamic 16


Esperado: “No se encontraron…” y tiempo bajo/estable.
Por qué: Reparte tareas en una cola, los hilos toman el “siguiente” trabajo; se adapta bien a latencias variables.

4.b) Secuencial por bloques (round_robin)
python finder.py 203.0.113.1 2 round_robin 16


Esperado: “No se encontraron…”.
Por qué: Partición en bloques contiguos. Sencillo; si algunos sufijos tardan más perderá algo de balanceo.

4.c) Estática aleatoria (random_static)
python finder.py 203.0.113.1 2 random_static 16


Esperado: “No se encontraron…”.
Por qué: Igual a 4.b, pero baraja primero para reducir sesgos del orden.

Interpretación: en redes reales, dynamic suele terminar igual o más rápido que las estáticas cuando hay heterogeneidad de latencias.

5) Caso “mixto” en LAN (ajustando sufijos)

Para IPs es más útil generar únicamente dígitos (p. ej. 00..99). Dos caminos:

Rápido (sin tocar código): usa length=0 para probar un host exacto (como 192.168.1.1).

Personalizado (requiere editar el script): cambia ALPHABET = "0123456789" y usa length apropiado.

Ejemplo (sin editar, solo host exacto):

python finder.py 192.168.1.1 0 dynamic
# Si responde tu router, deberías ver “Encontrados…”

6) Modo simulado (sin red / CI)

Activa el mock de ping via variable de entorno (si implementaste el bloque FINDER_MOCK):

Comando

# Simula éxitos cuando el host termina en "ok" o es localhost (ejemplo)
FINDER_MOCK=1 python finder.py base- 1 random_static


Esperado: “Encontrados…” solo cuando el sufijo simulado coincide con las reglas del mock.
Por qué: Permite probar lógica, hilos y división de trabajo sin depender de la red.

Interpretación de los resultados

“Encontrados (sufijos…)” → al menos un host respondió al ping.

“No se encontraron hosts válidos.” → ninguno respondió (posible: host inexistente, firewall, DNS que no resuelve, timeout corto, red caída).

Tiempo total: sirve para comparar estrategias y número de hilos. En general, más hilos ↓ tiempo hasta saturar CPU/red.

Variabilidad: latencias, pérdidas de paquetes y firewalls pueden afectar.
