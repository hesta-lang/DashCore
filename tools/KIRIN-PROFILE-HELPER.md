# Kirin Profile Helper (Termux)

Asistente para compilar y ejecutar el perfilador de CPU de DashCore directamente en un Android ARM64, incluido el Huawei Y9s.

## Requisitos

- Termux instalado.
- Conexión a Internet.
- Arquitectura Android ARM64 compatible con el benchmark.
- Espacio libre para instalar clang y Python.

## Instalación y ejecución

Abre Termux y copia el bloque completo:

```bash
pkg update
pkg install -y curl python clang
bash -c "$(curl -fsSL https://raw.githubusercontent.com/hesta-lang/DashCore/main/tools/install-kirin-profile-termux.sh)"
```

El asistente:
1. Descarga las fuentes públicas de DashCore desde GitHub Raw.
2. Compila el benchmark nativo en el propio teléfono usando Clang.
3. Verifica los archivos y la sintaxis Python.
4. Ejecuta el perfil local del dispositivo.
5. Guarda los informes en `$HOME/dashcore-kirin-profile/results`.

No descarga artefactos de GitHub Actions, por lo que evita el error HTTP 401 de la API de artifacts. Tampoco requiere ZArchiver ni extraer ZIP manualmente.

## Ajustar duración

Por defecto usa 2500 ms por prueba y 2 repeticiones. Para una medición más larga:

```bash
DASHCORE_CPU_DURATION_MS=4000 DASHCORE_CPU_REPETITIONS=3 bash -c "$(curl -fsSL https://raw.githubusercontent.com/hesta-lang/DashCore/main/tools/install-kirin-profile-termux.sh)"
```

## Límites y seguridad

- Solo compila y ejecuta un benchmark; no cambia frecuencias, gobernador, afinidad global, límites térmicos ni configuración persistente.
- El perfil mide el procesador físico disponible; no convierte AutoTest ni un emulador en una recreación completa del Kirin 710F.
- Comparte `kirin-cpu-profile.json` y `kirin-cpu-profile.md` para analizar los resultados.
