# Sistema de pruebas semiautomáticas

Este flujo combina el registro automático de Android Launcher con el analizador de sesiones de DashCore. No cambia el renderizado ni el gameplay.

## 1. Preparar el teléfono

1. Instala una versión `alpha` de Android Launcher que incluya **Export all sessions** en Launcher Diagnostics.
2. Instala el paquete DashCore de la rama experimental.
3. En Android Launcher, abre **Settings → Launcher Diagnostics** y deja activado **Automatic session recording**.
4. En Geometry Dash, abre los ajustes de DashCore y localiza **Capture object diagnostics**.

El launcher guarda automáticamente el dispositivo, versión del juego, uso de CPU/memoria, estado térmico disponible, intervalos de callbacks del renderer y marcadores de nivel. DashCore añade un resumen por nivel cuando su integración está disponible.

## 2. Ejecutar una comparación OFF/ON

Haz dos sesiones separadas. Cierra Geometry Dash completamente entre sesiones para que se escriban archivos independientes.

### Sesión A — captura OFF

- Desactiva **Capture object diagnostics**.
- Abre Geometry Dash y ejecuta el nivel elegido.
- Usa el mismo StartPos y recorrido que repetirás en la sesión B.
- Mantén una duración aproximada de 60–90 segundos dentro del nivel.
- Sal del nivel y cierra Geometry Dash normalmente.

### Sesión B — captura ON

- Activa **Capture object diagnostics**.
- Vuelve a abrir Geometry Dash.
- Repite el mismo nivel, StartPos, recorrido y duración.
- Cierra el juego normalmente.

Mantén iguales, en lo posible, la configuración gráfica, frecuencia de pantalla y aplicaciones en segundo plano. No cambies otras opciones entre sesiones.

**Importante:** el actual puente de DashCore envía marcadores de nivel sin ID/nombre en algunos casos. Por eso el comparador no puede demostrar por sí solo que dos recorridos sean idénticos. El operador debe confirmar manualmente el mismo nivel, ruta y StartPos; el comparador no marcará un par como controlado sin esa confirmación explícita.

## 3. Exportar desde el teléfono

1. Abre **Settings → Launcher Diagnostics**.
2. Comprueba que aparecen las dos sesiones recién grabadas.
3. Pulsa **Export all sessions**.
4. Guarda el ZIP y compártelo junto con una nota que indique cuál archivo corresponde a OFF y cuál a ON, además de confirmar que usaste el mismo nivel, ruta y StartPos.

El ZIP contiene los JSONL retenidos por el launcher. La exportación omite el archivo que todavía esté siendo grabado.

## 4. Analizar automáticamente

El analizador está en `scripts/analyze_launcher_diagnostics.py` y no requiere paquetes externos.

Resumen de todas las sesiones de un ZIP:

```bash
python3 scripts/analyze_launcher_diagnostics.py launcher-diagnostics.zip --output-dir reports/latest
``` 

Comparación directa de dos archivos JSONL, en el orden OFF y luego ON:

```bash
python3 scripts/analyze_launcher_diagnostics.py off.jsonl on.jsonl \
  --compare off.jsonl on.jsonl \
  --confirm-same-route \
  --output-dir reports/ab
``` 

El comando genera `launcher-diagnostics-summary.json` y `launcher-diagnostics-summary.md`. También acepta carpetas con archivos JSONL.

## 5. Qué valida y qué no

- Comprueba que el JSONL sea legible y marca registros incompletos, sesiones sin cierre, falta de muestras o marcadores de nivel.
- Resume promedio ponderado de intervalos, peor p95 por ventana, máximo observado, callbacks CPU/wall, CPU de proceso, PSS, memoria disponible y métricas de captura de DashCore.
- Distingue los recuentos de ventanas con un máximo superior a 50/100/250 ms de un recuento exacto de todos los frames lentos.
- Solo compara OFF contra ON cuando coinciden el dispositivo, Android, juego, frecuencia/presupuesto de frames, existe captura OFF/ON y el usuario confirma la misma ruta/StartPos.
- Si el launcher no aporta ID/nombre de nivel, utiliza los datos agregados del segmento de nivel únicamente después de la confirmación manual.
- Informa cambios porcentuales; no declara una mejora automáticamente.

Los intervalos y duraciones de callbacks son mediciones de CPU/cadencia del callback. No son tiempos de ejecución de GPU ni mediciones exactas de presentación de pantalla. Un resultado de emulador no sustituye una prueba en el Huawei físico.

## 6. Validación del analizador en GitHub Actions

El workflow Android64 de la rama `experimental-renderer` ejecuta las pruebas unitarias del analizador antes de compilar DashCore. Así se comprueba el procesamiento de JSONL, la lectura de ZIP, los registros corruptos y las condiciones del comparador en cada cambio de código.
