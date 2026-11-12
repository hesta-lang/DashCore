# Diseño: renderizador experimental progresivo de DashCore

**Fecha:** 2026-11-12  
**Rama:** `experimental-renderer`  
**Base:** `main` en `a4341ceab9a38c5adf7277000b65cfd9a6c1771f`  
**Plataforma inicial:** Geometry Dash 2.2081, Geode 5.11.2, Android64 (`arm64-v8a`), Android 10 / API 29, Huawei Kirin 710F / Mali-G51 MP4.

## 1. Objetivo

Desarrollar en una rama aislada un renderizador original para los objetos de los niveles de Geometry Dash. El objetivo final es sustituir progresivamente el renderizado vanilla de objetos, no reemplazar todo el motor gráfico del juego.

El trabajo empieza con un pipeline paralelo que recopila y prepara información sin emitir dibujo. Solo después de demostrar que reproduce correctamente el estado visual y el orden de dibujo se habilitará el reemplazo de una categoría pequeña de objetos. El renderizador original seguirá disponible como fallback durante todas las etapas experimentales.

## 2. Alcance y límites

### Incluido

- Actualizaciones de compatibilidad necesarias para Geode 5.11.2 y Geometry Dash 2.2081.
- Arquitectura modular del nuevo pipeline de objetos.
- Recopilación de objetos y datos visuales.
- Modelado de sprites, atlas/texturas, transformaciones, color, opacidad, blending y capas de orden.
- Organización de batches y preparación de geometría.
- Backend gráfico Android compatible con las capacidades GLES realmente detectadas.
- Sustitución gradual y reversible del dibujo vanilla de categorías concretas de objetos.
- Fallback automático al renderizador original ante datos incompletos, capacidades ausentes o errores.

### Excluido

- Reemplazar el renderizado de shaders globales, fondos, efectos globales, partículas ajenas al alcance validado, interfaz, menús, HUD, editor y elementos de jugador.
- Cambiar física, colisiones, triggers, lógica de grupos, gameplay, tiempos, input o comportamiento de objetos.
- Cambiar governors, frecuencias, scheduler, cpuset o políticas térmicas del dispositivo.
- Añadir al producto módulos de diagnóstico, perfiles de rendimiento o herramientas AutoTest. Las pruebas automatizadas y la validación externa seguirán utilizándose durante el desarrollo.
- Copiar o enlazar código fuente de Bismuth.

## 3. Principios de seguridad y compatibilidad

1. **Vanilla primero:** mientras una categoría no haya superado las pruebas, se dibuja exclusivamente con el sistema original.
2. **Sin doble dibujo:** un objeto pertenece al renderizador vanilla o al experimental en un frame, nunca a ambos.
3. **Fail-open:** ante un objeto no representable, un recurso desconocido, un estado de blending no soportado o una inconsistencia, se delega ese objeto/categoría al renderer vanilla.
4. **Orden exacto:** preservar el orden de capas, orden local, orden de hijos y las reglas de mezcla. No agrupar sprites si la agrupación cambia el resultado visual.
5. **Estado dinámico actualizado:** no congelar valores que cambian por triggers, grupos, pulsos, color, rotación, escala, opacidad, animaciones o movimiento.
6. **No asumir equivalencias:** un GameObject no equivale necesariamente a un sprite ni a un quad.
7. **Android real como objetivo:** no considerar que una prueba en emulador/SwiftShader demuestra compatibilidad o mejora en el Mali-G51.
8. **Sin APIs inventadas:** usar bindings de Geode para GD 2.2081 y validar disponibilidad/ABI de cada método usado. No usar offsets adivinados.
9. **Licencia y clean-room:** Bismuth se usa solo como referencia de conceptos públicos y comportamiento documentado. La implementación, estructuras, shaders y código serán originales; no se copiará código de Bismuth ni se distribuirán binarios derivados de su fuente modificada por IA. Revisar las licencias de cualquier otra dependencia antes de incorporarla.

## 4. Arquitectura propuesta

El sistema se dividirá en componentes pequeños, sin acoplar el recolector a una API gráfica concreta:

- **LevelObjectCollector:** obtiene la lista de objetos del nivel y sus referencias visuales mediante APIs/bindings verificados. No modifica el árbol Cocos.
- **RenderSnapshot:** representación propia e inmutable por frame de los datos necesarios para decidir qué puede dibujarse. Debe conservar identificadores y referencias seguras; no guardar punteros que sobrevivan a la destrucción del objeto.
- **SpriteDescriptor:** describe textura/atlas, rectángulo UV, geometría local, ancla, transformaciones, color, opacidad, blending y estado de visibilidad.
- **LayerOrderModel:** representa las capas y el orden estable de dibujo. Nunca reordena entre capas solo para mejorar batching.
- **BatchPlanner:** agrupa únicamente sprites compatibles por textura/atlas, blending y capa, preservando el orden original dentro de cada grupo.
- **RenderBackend:** interfaz para cargar recursos, actualizar buffers y emitir batches. El primer backend será Android GLES y solo utilizará capacidades verificadas en tiempo de ejecución.
- **VanillaFallback:** conserva el camino de dibujo original para categorías no migradas y casos no compatibles.
- **RendererMode:** control interno de etapa: observación paralela, comparación sin dibujo, o reemplazo experimental acotado. En builds iniciales el modo de dibujo experimental permanecerá desactivado.

El diseño no presupone que compute shaders, SSBO, indirect draw o GLES 3.1 estén disponibles. Primero se consulta el contexto GLES real y las extensiones. El backend inicial puede utilizar CPU y buffers GLES convencionales; compute shaders serán una optimización posterior opcional, nunca requisito de arranque.

## 5. Etapas de implementación y puertas de aceptación

### Etapa 0 — Separación y compatibilidad

- Mantener el desarrollo en `experimental-renderer`, sin alterar `main`.
- Separar el producto experimental de los módulos de diagnóstico/perfilado/AutoTest de DashCore.
- Conservar solo los cambios de compatibilidad y las dependencias estrictamente necesarias para el renderizador.
- Añadir build Android64 y controles estáticos en CI.

**Aceptación:** build Android64 correcto; mod carga en Geode; sin hooks de dibujo experimental activos; sin cambios visuales ni de gameplay.

### Etapa 1 — Pipeline paralelo sin dibujo

- Crear las interfaces y estructuras de datos del pipeline.
- Recopilar el inventario de objetos y los datos visuales que se puedan obtener de forma segura.
- Manejar creación, destrucción, reinicio y transición de nivel sin referencias colgantes.
- No interceptar ni reemplazar llamadas de dibujo.

**Aceptación:** el pipeline puede iniciarse y limpiarse repetidamente durante carga/salida de niveles; no modifica nodos ni emite comandos gráficos; no hay crashes ni fugas evidentes.

### Etapa 2 — Modelo de sprites y orden

- Construir descriptores de sprites y recursos.
- Resolver atlas, UV, textura y blending con datos verificables.
- Modelar capas, orden de hijos y orden de sprites.
- Identificar explícitamente objetos con representación incompleta; esos objetos quedan fuera del reemplazo.

**Aceptación:** cobertura documentada y determinista; orden del modelo coincide con el orden vanilla para todos los elementos representados; los elementos ambiguos se excluyen.

### Etapa 3 — Batching y geometría en paralelo

- Construir lotes por combinaciones compatibles de capa, textura/atlas y blending.
- Preparar vértices/índices y buffers sin enviarlos a pantalla.
- Mantener los estados dinámicos separados de los datos estáticos.
- Verificar límites de buffers y reconstrucción tras cambios de nivel o recursos.

**Aceptación:** los lotes no cruzan límites de orden/blending; geometría y UV son válidas; no hay comandos de dibujo experimental ni cambios visuales.

### Etapa 4 — Comparación de salida sin presentar

- Generar la salida experimental en un destino de prueba o comparar estructuras/estadísticas de geometría, sin sustituir el framebuffer del juego.
- Validar muestras de objetos simples, atlas, blending, color, opacidad, escalado, rotación y orden.
- Investigar diferencias antes de activar cualquier categoría.

**Aceptación:** las categorías candidatas pasan pruebas visuales y estructurales definidas; cualquier diferencia no explicada bloquea el avance.

### Etapa 5 — Primer reemplazo acotado

- Elegir una categoría pequeña, estática y visualmente simple.
- Activar el reemplazo solo para objetos clasificados como totalmente compatibles.
- Mantener vanilla para todos los demás objetos.
- Añadir un interruptor experimental desactivado por defecto y fallback seguro ante fallo.

**Aceptación:** sin objetos ausentes/duplicados, flicker, cambios de orden, cambios de gameplay o crashes en pruebas repetidas; la ruta vanilla sigue funcionando al desactivar el experimento.

### Etapa 6 — Expansión progresiva

Migrar una categoría por vez. Antes de cada expansión, validar por separado:

- sprites base, detail, glow y black;
- múltiples spritesheets/atlas;
- blending y opacidad;
- colores, pulsos y color triggers;
- rotación, escala, grupos y movimiento;
- objetos animados, efectos y casos especiales.

Triggers, objetos agrupados, efectos, objetos dinámicos y cualquier categoría cuya equivalencia no esté demostrada permanecen en vanilla.

### Etapa 7 — Optimizaciones del backend

Solo después de tener equivalencia visual y funcional:
- reducir cambios de textura y estado;
- reutilizar buffers;
- actualizar únicamente datos dinámicos;
- evaluar culling conservador y chunking;
- evaluar compute shader si el dispositivo y el contexto lo soportan.

Cada optimización debe demostrar que reduce trabajo real sin introducir diferencias. No se publicará una afirmación de mejora sin medición física repetible en el Huawei Y9s.

## 6. Pruebas obligatorias

- Compilación Android64 para la combinación objetivo de Geode/GD.
- Carga y descarga del mod, entrar/salir de niveles y reiniciar niveles.
- Niveles con muchos objetos, capas, grupos, triggers, portales, orbes, partículas y objetos animados.
- Cambios de color, pulsos, opacidad, rotación, escala y movimiento.
- Capturas comparables vanilla/experimental en el mismo dispositivo, resolución y estado del nivel.
- Verificación de ausencia de objetos omitidos, duplicados, parpadeo, cambios de z-order y alteraciones de gameplay.
- Prueba explícita de fallback con el backend experimental desactivado y con recursos/estados no soportados.
- Pruebas en dispositivo físico para aceptación de rendimiento; AutoTest/emulador solo valida carga, estabilidad y automatización básica.

Los builds de diagnóstico o validación, si se generan, llevarán etiqueta prerelease `alpha`. No se publicará una versión de optimización sin esa etiqueta hasta demostrar una mejora real y validada.

## 7. Condiciones de no-go

Detener la migración y mantener vanilla si ocurre cualquiera de estos casos:

- No se puede reconstruir con certeza el orden de dibujo.
- El recolector no representa todos los sprites de una categoría.
- Se desconoce el estado de blending, textura, color o transformación de un sprite.
- Una API/binding no está disponible o no es segura en Android64.
- Se detecta cualquier cambio de gameplay, timing, colisión o lógica de triggers.
- El backend falla y no puede delegar de forma segura al renderer original.
- La mejora de rendimiento no se puede demostrar en hardware físico.

## 8. Decisiones pendientes de implementación

Estas decisiones se resolverán con evidencia durante las etapas correspondientes, no por suposición:

- APIs exactas para extraer cada atributo visual y ciclo de vida.
- Formato interno de vértices/índices y estrategia de buffers.
- Categoría inicial del primer reemplazo.
- Uso de CPU frente a GLES convencional o compute shader.
- Compatibilidad con dispositivos GLES distintos del Huawei objetivo.

## 9. Resultado esperado

Una rama experimental que evoluciona desde un pipeline paralelo de solo lectura hasta un renderizador propio para objetos de niveles, con sustitución gradual, fallback vanilla y sin alterar shaders globales, UI, editor, física ni gameplay. La rama principal de DashCore no recibe cambios hasta que una etapa esté validada y se decida integrarla.
