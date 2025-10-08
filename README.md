# README — Finder (búsqueda distribuida por sufijos)

Este proyecto busca un host “perdido” realizando pings concurrentes sobre una base o prefijo concatenado con sufijos generados automáticamente (a–z, 0–9) de longitud L, utilizando múltiples hilos y diferentes estrategias de división de trabajo (dynamic, round_robin, random_static).

El objetivo es demostrar paralelismo, coordinación entre hilos y estrategias de asignación de trabajo para tareas IO-bound como los pings.

## Versiones disponibles
1. finder.c — Implementación en C (multithread con pthread)
2. finder.py — Versión equivalente en Python (multiprocessing/threading)

Ambas cumplen la misma lógica general y permiten comparar rendimiento entre lenguajes.

## Requisitos
Para la versión en C:

Linux, macOS o WSL (Windows Subsystem for Linux)

gcc y librerías de desarrollo (paquete build-essential)

Acceso al comando ping

Linux/macOS: ping -c

Windows: ping -n (si compilas con MinGW o MSYS2)

Instalación de dependencias básicas en Ubuntu:

sudo apt update
sudo apt install -y build-essential


Compilación:

gcc -O2 -pthread finder.c -o finder

Para la versión en Python:

Python 3.8+

Acceso a ping (igual que en C)

Red funcionando (o variable de entorno FINDER_MOCK para modo simulado)

Comprobación rápida:

python --version

## Concepto general

Se generan todos los posibles sufijos de longitud L usando los caracteres [a–z0–9].

Cada sufijo se concatena con la base para formar un posible host.

Cada hilo intenta hacer ping a su host y reporta los que respondan.

Según el modo de trabajo, los hilos comparten una cola (dynamic) o procesan bloques estáticos (round_robin, random_static).

## Uso básico
En C:
./finder <base> <length> <variant> <workers>

En Python:
python finder.py <base> <length> <variant> [n_workers] [--verbose] [--selftest]

Parámetros comunes:
Parámetro	Descripción
base	Prefijo o parte inicial (por ej. 127.0.0. o srv-)
length	Longitud del sufijo (1 → a..z,0..9)
variant	dynamic, round_robin, random_static
workers	Número de hilos o procesos concurrentes
--verbose	(Python) salida detallada
--selftest	(Python) ejecuta pruebas internas
- Pruebas recomendadas (y por qué)
## 1) Prueba positiva mínima

C:

./finder 127.0.0. 1 dynamic 2


Python:

python finder.py 127.0.0.1 0 dynamic


- Resultado esperado:

Encontrados (sufijos con ping OK): 0, 1, 2, 3, ...


Por qué:
Los hosts 127.0.0.0–127.0.0.9 pertenecen a loopback, por lo tanto responden al ping.

## 2) Prueba negativa mínima

C:

./finder 203.0.113. 1 dynamic 2


Python:

python finder.py 203.0.113.1 0 dynamic


- Resultado esperado:

No se encontraron hosts válidos.


Por qué:
203.0.113.0/24 está reservado para documentación (TEST-NET-3), y normalmente no responde.

## 3) Autotest integrado (solo en Python)
python finder.py --selftest


Resultado esperado:

== Selftest 1: 127.0.0.1 (debe ENCONTRAR) ==
Hits: ['']
OK en X.XXX s

== Selftest 2: 203.0.113.1 (NO debe encontrar) ==
Hits: []
OK en X.XXX s


Por qué:
Verifica que la función ping_host y la coordinación de hilos funcionen correctamente.

## 4) Comparar estrategias de trabajo
a) Dinámica (cola compartida)
./finder 203.0.113. 1 dynamic 8


Esperado: No se encuentran hosts, tiempo uniforme.
Por qué: balanceo óptimo de carga entre hilos.

b) Round Robin (bloques secuenciales)
./finder 203.0.113. 1 round_robin 8

c) Estática aleatoria (bloques barajados)
./finder 203.0.113. 1 random_static 8


Interpretación:

dynamic tiende a ser más eficiente si hay latencias variables.

round_robin es más simple pero puede tener desequilibrio.

random_static mezcla el orden para mejorar balanceo sin cola.

## 5) Modo simulado (sin red)

Puedes reemplazar ping_host por un mock que devuelve éxitos aleatorios:

static int ping_host(const char *host) {
    usleep(1000 + rand() % 5000);
    return (rand() % 10) == 0;  // 10% de éxito aleatorio
}


O, en Python, define:

FINDER_MOCK=1 python finder.py base- 1 random_static


Por qué:
Permite probar concurrencia sin depender de conexión de red o permisos ICMP.

## Precauciones

36^length crece exponencialmente (usa length ≤ 3 para evitar saturar memoria o CPU).

Si la ejecución se alarga, usa Ctrl + C para detenerla.

Evita escanear redes que no te pertenecen (puede considerarse intrusivo).

## Interpretación de resultados
Salida	Significado
Encontrados (sufijos con ping OK): ...	Al menos un host respondió
No se encontraron hosts válidos.	Ninguno respondió (o red bloqueada)
bash: syntax error near unexpected token	Copiaste la salida como comando
^[[200~	Artefacto de pegado desde portapapeles en terminal
No such file or directory	No estás en el directorio del binario o lo borraste
## Escalado y memoria estimada
Length	Combinaciones (36^L)	Aprox. memoria
1	36	<1 KB
2	1,296	<1 MB
3	46,656	<10 MB
4	1.68 M	>200 MB
5	60 M	¡Muy pesado! 
- Subdominios (prefijo antes del dominio)

Para probar subdominios (a.example.com, b.example.com), el programa debe construir sufijo + base en vez de base + sufijo.

Agregar en finder.c:

int prefix = 0;
const char *env = getenv("FINDER_PREFIX");
if (env && strcmp(env, "1") == 0) prefix = 1;


Y en la sección donde se construye el host:

if (prefix)
    snprintf(host, len, "%s%s", suf, a->base);
else
    snprintf(host, len, "%s%s", a->base, suf);


Luego ejecutar:

FINDER_PREFIX=1 ./finder .example.com 1 random_static 8

 ## Alternativa con Docker

Sin necesidad de instalar gcc localmente:

docker run --rm -it -v "$PWD":/work -w /work gcc:12 bash -lc \
"gcc -O2 -pthread finder.c -o finder && ./finder 127.0.0. 1 dynamic 2"
