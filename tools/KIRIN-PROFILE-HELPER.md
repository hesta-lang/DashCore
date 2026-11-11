# Kirin Profile Helper (Termux)

Herramienta auxiliar para ejecutar el perfilador de CPU de DashCore directamente en un Android ARM64, incluido el Huawei Y9s.

## Requisitos

- Termux instalado desde una fuente compatible.
- Conexión a Internet.
- Acceso de lectura/escritura al almacenamiento privado de Termux.
- Arquitectura ARM64 para ejecutar el binario de perfilado.

## Instalación y ejecución

Abre Termux y ejecuta este bloque:

```bash
pkg update
pkg install -y curl python unzip
bash -c "$(curl -fsSL https://raw.githubusercontent.com/hesta-lang/DashCore/main/tools/install-kirin-profile-termux.sh)"
```

El asistente:
1. Consulta los artefactos públicos de GitHub Actions de DashCore.
2. Descarga el artefacto vigente más reciente llamado `DashCore-Kirin-CPU-Profile-Tool`.
3. Extrae los archivos en `$HOME/dashcore-kirin-profile`, sin depender de la carpeta Download ni de ZArchiver.
4. Comprueba que existan el binario ARM64, el perfil JSON y el script Python.
5. Ejecuta el perfil local y guarda los informes en `$HOME/dashcore-kirin-profile/results`.

## Ajustar duración

Por defecto usa 2500 ms por prueba y 2 repeticiones. Se puede ajustar antes de ejecutar:

```bash
DASHCORE_CPU_DURATION_MS=4000 DASHCORE_CPU_REPETITIONS=3 bash -c "$(curl -fsSL https://raw.githubusercontent.com/hesta-lang/DashCore/main/tools/install-kirin-profile-termux.sh)"
```

## Notas

- No requiere descargar ni extraer manualmente el ZIP de Actions.
- El repositorio debe ser público o Termux debe disponer de autenticación compatible para consultar artefactos privados.
- Los artefactos de GitHub Actions pueden expirar según la política de retención del repositorio.
- El perfil mide el procesador físico disponible; no convierte un emulador en una recreación completa del Kirin 710F.
- No aplica límites de CPU, cambios de gobernador ni ajustes térmicos al teléfono.
- Comparte los archivos `kirin-cpu-profile.json` y `kirin-cpu-profile.md` generados para analizar los resultados.
