# Inventario de bindings de renderizado y objetos — Geometry Dash 2.2081

**Proyecto:** DashCore (Android64 / Geode)  
**Objetivo:** documentar las APIs de Geode que permiten estudiar el ciclo de dibujo, los sprites, los lotes de renderizado y el ciclo de vida de los objetos del nivel.  
**Referencia de bindings:** [geode-sdk/bindings, commit `6eb417ad2810fe43188bb94b6097562b201db0fa`](https://github.com/geode-sdk/bindings/tree/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081).

> Este inventario es una guía de investigación de las clases y funciones relevantes, no una afirmación de que cada función pueda ser llamada o interceptada en Android sin validación adicional. No contiene offsets Android inventados ni propone reemplazar el renderer original.

## 1. Archivos de referencia

| Archivo | Contenido útil |
|---|---|
| [Cocos2d.bro](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro) | Jerarquía de nodos, sprites, batches, atlas, texturas, cámara, partículas y ciclo de dibujo |
| [GeometryDash.bro](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/GeometryDash.bro) | Clases específicas del juego: capas, objetos, editor y triggers |
| [Extras.bro](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Extras.bro) | Estructuras auxiliares y tipos adicionales |
| [inline/](https://github.com/geode-sdk/bindings/tree/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline) | Implementaciones auxiliares disponibles para la versión 2.2081 |

## 2. Ciclo de renderizado Cocos2d

### 2.1 CCDirector — ciclo de frame

Declaración: [Cocos2d.bro, CCDirector](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L848-L881)

| Método | Función para la investigación |
|---|---|
| `drawScene()` | Punto de entrada para medir la fase de dibujo de la escena |
| `calculateDeltaTime()` | Cálculo del delta temporal |
| `calculateMPF()` | Medición temporal por frame |
| `applySmoothFix()` | Ajuste del delta temporal |

DashCore ya mide `CCDirector::drawScene`; este punto debe seguir siendo la referencia global de coste de dibujo. No se debe confundir duración de `drawScene` con coste exclusivo de GPU: incluye trabajo de CPU y recorrido de escena.

### 2.2 CCNode — jerarquía y recorrido de escena

Declaración: [Cocos2d.bro, CCNode](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L2619-L2745)

| Método | Función |
|---|---|
| `visit()` | Recorre el nodo y su jerarquía para procesar el dibujo |
| `draw()` | Dibujo del nodo |
| `addChild()` | Inserta un nodo hijo |
| `removeChild()`, `removeFromParent()` | Desvincula nodos |
| `reorderChild()` | Cambia el orden de un hijo |
| `sortAllChildren()` | Ordena los hijos |
| `nodeToParentTransform()` | Calcula la transformación hacia el nodo padre |
| `nodeToParentTransformFast()` | Variante rápida de la transformación |
| `updateTransform()` | Actualiza la transformación del nodo |

**Importancia:** `visit()` es un punto de observación de alto volumen. Un hook global costoso sobre todos los nodos podría añadir más trabajo del que se pretende ahorrar. La instrumentación debe ser ligera, muestreada y filtrada por los nodos relevantes del nivel.

### 2.3 CCSprite — dibujo individual

Declaración: [Cocos2d.bro, CCSprite](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L4031-L4150)

| Método | Función |
|---|---|
| `create()`, `create(char const*)` | Crea sprites |
| `createWithSpriteFrame()`, `createWithSpriteFrameName()` | Crea sprites desde frames |
| `createWithTexture()` | Crea sprites desde una textura |
| `initWithFile()`, `initWithSpriteFrame()`, `initWithSpriteFrameName()` | Inicializa el sprite |
| `draw()` | Dibujo del sprite |
| `updateTransform()` | Actualiza su transformación |
| `setTextureRect()` | Cambia el rectángulo de textura |
| `updateBlendFunc()` | Actualiza la función de mezcla |

Estos métodos permiten separar el coste de sprites individuales del coste de los lotes. No hay que asumir que cada `GameObject` equivale a una sola llamada a `CCSprite::draw`.

### 2.4 CCSpriteBatchNode — agrupamiento de sprites

Declaración: [Cocos2d.bro, CCSpriteBatchNode](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L4157-L4205)

| Método | Función |
|---|---|
| `create()`, `createWithTexture()` | Crea un batch |
| `addChild()`, `appendChild()`, `insertChild()` | Añade sprites al batch |
| `insertQuadFromSprite()` | Inserta geometría de un sprite en el atlas |
| `updateQuadFromSprite()` | Actualiza geometría ya insertada |
| `removeSpriteFromAtlas()` | Retira un sprite del atlas |
| `reorderBatch()` | Reorganiza el batch |
| `draw()`, `visit()` | Procesa y dibuja el batch |

Es uno de los objetivos principales de investigación para DashCore. Antes de diseñar otro sistema de batching se debe medir cuánto trabajo ya resuelve el batch original y qué reglas de orden, mezcla y actualización necesita preservar.

### 2.5 CCTextureAtlas — quads y dibujo por lotes

Declaración: [Cocos2d.bro, CCTextureAtlas](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L4427-L4470)

| Método | Función |
|---|---|
| `drawQuads()` | Dibuja los quads almacenados |
| `drawNumberOfQuads()` | Dibuja una cantidad o rango de quads |
| `insertQuad()`, `insertQuads()` | Inserta geometría |
| `updateQuad()` | Actualiza geometría |
| `removeQuadAtIndex()`, `removeQuadsAtIndex()`, `removeAllQuads()` | Elimina geometría |

Estos métodos permiten investigar si el coste procede de insertar/actualizar quads, ordenar sprites o ejecutar el dibujo del lote.

## 3. Texturas, frames y recursos gráficos

### 3.1 CCTexture2D y CCTextureCache

Declaraciones: [CCTexture2D](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L4379-L4426) · [CCTextureCache](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L4472-L4499)

Métodos de interés:
- `CCTexture2D::initWithData()`, `initWithImage()`, `initWithETCFile()`, `drawAtPoint()`, `drawInRect()`.
- `CCTextureCache::addImage()`, `addImageAsync()`, `textureForKey()`, `removeUnusedTextures()`, `removeTextureForKey()`.

Objetivo: identificar carga repetida, presión de memoria y reutilización de texturas. No se recomienda expulsar texturas del cache durante gameplay sin demostrar que no se vuelven a solicitar en el mismo frame o en efectos posteriores.

### 3.2 CCSpriteFrame y CCSpriteFrameCache

Declaración: [CCSpriteFrameCache](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L4244-L4265)

Métodos de interés:
- `addSpriteFramesWithFile()`
- `addSpriteFramesWithDictionary()`
- `spriteFrameByName()`
- `removeUnusedSpriteFrames()`
- `removeSpriteFramesFromTexture()`

Implementación 2.2081: [inline/CCSpriteFrameCache.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/CCSpriteFrameCache.cpp).

### 3.3 CCAnimation

Declaración en [Cocos2d.bro](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L294-L360). Permite crear animaciones a partir de arrays de frames (`createWithSpriteFrames()`, `initWithAnimationFrames()`). Es importante para distinguir sprites estáticos de objetos animados que no deben recibir las mismas reglas de descarte.

## 4. Efectos, recorte y shaders

| Clase | Métodos o miembros relevantes | Utilidad |
|---|---|---|
| `CCParticleSystem` | `create()`, `createWithTotalParticles()`, `initWithFile()`, `updateQuadWithParticle()`, blend functions | Medir y clasificar coste de partículas |
| `CCParticleSystemQuad` | `draw()`, `updateQuadWithParticle()` | Observar dibujo de partículas |
| `CCRenderTexture` | `create()`, `begin()`, `beginWithClear()`, `draw()`, `visit()` | Detectar renderizado fuera de pantalla |
| `CCClippingNode` | `visit()`, `setStencil()`, `setInverted()` | Comprender recortes y stencils |
| `CCCamera` | Estado de cámara de Cocos2d | Relacionar transformaciones de cámara con coordenadas de escena |
| `CCGLProgram` | `compileShader()`, `initWithVertexShaderByteArray()`, `setUniformLocationWith*()`, `updateUniforms()` | Identificar programas y cambios de uniforms |

Referencias: [CCParticleSystem](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L3088-L3371), [CCRenderTexture](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L3564-L3582), [CCClippingNode](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L671-L692), [CCGLProgram](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/Cocos2d.bro#L1600-L1636).

## 5. Creación y ciclo de vida de objetos de Geometry Dash

### 5.1 GameObject — objeto de juego como jerarquía gráfica

Implementación 2.2081: [inline/GameObject.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/GameObject.cpp).

Hallazgos confirmados:
- `GameObject::init(frame)` inicializa mediante `CCSpritePlus::initWithSpriteFrameName(frame)` y ejecuta `commonSetup()`.
- `GameObject::addInternalChild()` crea un `CCSprite` usando `CCSprite::createWithSpriteFrameName()`, calcula su posición relativa y lo añade como hijo.
- El archivo también contiene creación y actualización de sprites de color, glow, contenedores de color/opacidad y otros elementos visuales.

**Consecuencia:** un `GameObject` puede representar varios nodos o sprites Cocos2d. Un censo de `PlayLayer::m_objects` no equivale a un censo completo de llamadas de dibujo, quads o nodos visibles.

### 5.2 GJBaseGameLayer — objetos activos y secciones

Implementación 2.2081: [inline/GJBaseGameLayer.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/GJBaseGameLayer.cpp).

Hallazgos confirmados:
- `addToObjectsToShow(GameObject*)` administra la lista `m_activeObjects` mediante `m_activeObjectsCount` y `m_activeObjectsIndex`.
- `reorderObjectSection(GameObject*)` retira el objeto de su sección y lo vuelve a insertar.
- `updateAllObjectSection()` recorre `m_objects` y llama a `updateObjectSection()`.
- El código también mueve objetos entre secciones cuando cambian sus posiciones.

**Consecuencia:** el juego ya dispone de organización espacial/seccional. DashCore debe entender cómo funciona antes de introducir chunks, listas de visibilidad o un segundo sistema de clasificación.

### 5.3 LevelEditorLayer — materialización y administración de objetos

Implementación 2.2081: [inline/LevelEditorLayer.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/LevelEditorLayer.cpp).

Hallazgos confirmados:
- `addObjectFromVector()` construye el objeto con `GameObject::objectFromVector()`, aplica comprobaciones, lo oculta inicialmente y lo registra con `addToSection()` y `addSpecial()`.
- `getAllObjects()` devuelve `m_objects`.
- `removeAllObjects()` copia la colección y elimina cada objeto mediante `removeObject()`.
- `sortBatchnodeChildren()` recorre `m_batchNodes` y ejecuta `manualSortAllChildren()` en cada batch.

**Consecuencia:** hay una ruta concreta de construcción y registro de objetos y también una colección de batch nodes administrada por el editor. Debemos comparar su uso durante edición y gameplay antes de aplicar una optimización compartida.

### 5.4 PlayLayer y DrawGridLayer

- [inline/PlayLayer.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/PlayLayer.cpp) confirma recorridos de `m_objects`, el escaneo de objetos activos guardables y funciones específicas de la partida.
- [inline/DrawGridLayer.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/DrawGridLayer.cpp) mantiene colecciones separadas para efectos, guías y objetos de velocidad (`m_effectGameObjects`, `m_guideObjects`, `m_speedObjects`).

No deben mezclarse esas listas auxiliares con el inventario primario de objetos del nivel.

### 5.5 ObjectToolbox — catálogo de objetos

La clase `ObjectToolbox` forma parte de los bindings de Geometry Dash, pero su archivo inline 2.2081 solo muestra `allKeys()` dentro de bloques de compilación para Windows/iOS y devuelve `nullptr` en ese stub: [inline/ObjectToolbox.cpp](https://github.com/geode-sdk/bindings/blob/6eb417ad2810fe43188bb94b6097562b201db0fa/bindings/2.2081/inline/ObjectToolbox.cpp).

Por lo tanto, `ObjectToolbox::allKeys()` **no queda validado como ruta de enumeración utilizable en Android**. Para el análisis del nivel, el camino confirmado por las implementaciones consultadas es `LevelEditorLayer::addObjectFromVector()` → `GameObject::objectFromVector()` y la colección `m_objects`.

## 6. Mapa de investigación priorizado para DashCore

| Prioridad | APIs | Pregunta que debe responderse |
|---|---|---|
| P0 | `CCNode::visit()`, `CCSprite::draw()`, `CCSpriteBatchNode::draw()`, `CCTextureAtlas::drawQuads()` | ¿En qué capa se concentra el coste de CPU/dibujo y cuántos nodos/quads se procesan? |
| P0 | `GameObject::init()`, `addInternalChild()`, `GJBaseGameLayer::m_objects`, árbol Cocos | ¿Cuántos sprites y nodos visuales produce cada GameObject? |
| P0 | `LevelEditorLayer::m_batchNodes`, `sortBatchnodeChildren()` | ¿Qué lotes usa realmente el juego y cuándo los reordena? |
| P1 | `updateObjectSection()`, `reorderObjectSection()`, `addToObjectsToShow()` | ¿Qué organización espacial/activa ya ofrece Geometry Dash? |
| P1 | `CCTextureCache`, `CCSpriteFrameCache`, `CCTexture2D` | ¿Hay cargas repetidas, misses de cache o presión de memoria? |
| P2 | `CCParticleSystemQuad`, `CCRenderTexture`, `CCClippingNode`, `CCGLProgram` | ¿Qué coste adicional aportan partículas, máscaras, render textures y shaders? |

### Secuencia segura de trabajo

1. Capturar un nivel de referencia sin culling, registrando tiempo de `drawScene`, `CCNode::visit`, sprites dibujados, batches y quads cuando las APIs permitan medirlos sin alterar su comportamiento.
2. Correlacionar cada `GameObject` con sus nodos hijos, sprites auxiliares y batch node. No asumir correspondencia uno-a-uno.
3. Mapear las secciones y listas activas ya existentes en Geometry Dash.
4. Comparar coste de dibujo individual, coste de batch y coste de preparación/ordenamiento. Añadir solo instrumentación muestreada.
5. Diseñar una optimización sobre el cuello de botella demostrado. Mantener orden Z, blending, colores, glow, animaciones, triggers, colisiones y objetos del jugador.

## 7. Disponibilidad Android y límites de los bindings

- En `Cocos2d.bro`, las clases centrales listadas arriba aparecen bajo anotaciones como `[[link(win, android)]]`. Eso identifica el ámbito de enlace declarado para la clase.
- Las direcciones visibles en estas declaraciones incluyen etiquetas como `imac`, `m1` e `ios`; **no son offsets Android**. No copiar una dirección Mac/Apple Silicon como si fuera una dirección ARM64 Android.
- La disponibilidad de una clase no demuestra por sí sola que todos sus métodos puedan interceptarse desde el mod Android. Cada hook debe compilar con el SDK objetivo y probarse en el dispositivo.
- Algunas implementaciones de `inline/*.cpp` contienen cuerpos vacíos o están dentro de bloques `#if` específicos de otras plataformas. Esos stubs no revelan el comportamiento real de la función en Android. Por ejemplo, un `update(float)` vacío en un archivo inline no debe interpretarse como que Geometry Dash no actualiza ese objeto.
- El inventario de objetos, el árbol Cocos y los quads del atlas son tres niveles diferentes. Ninguno sustituye por sí solo a los otros dos.

## 8. Restricciones para la primera implementación

1. No interceptar de forma costosa todos los `CCNode::visit()` en cada frame.
2. No saltar `visit()` o `draw()` de un padre completo sin probar el efecto sobre todos sus descendientes.
3. Mantener protegidos jugadores, triggers, efectos, objetos agrupados/dinámicos, objetos animados y cualquier objeto con bounds o relaciones desconocidas.
4. No eliminar ni recrear sprites/texturas como supuesto método de culling.
5. Mantener `experimental-render-culling=false` por defecto hasta validar una optimización real en el Huawei Y9s.
6. Mantener la UI y el gameplay vanilla; las pruebas diagnósticas deben ser opt-in.

## 9. Resultado

La evidencia consultada favorece investigar primero el renderer original de Cocos2d y los batch nodes que ya administra Geometry Dash. La siguiente fase no debe ser copiar un renderer alternativo a ciegas, sino medir la ruta real:

`GameObject / sprites hijos → CCNode tree y secciones → CCSpriteBatchNode / CCTextureAtlas → draw`

El inventario no modifica código de ejecución ni activa una optimización. Es la base técnica para la siguiente etapa de instrumentación y análisis.

## 10. Runtime render-pipeline diagnostics in DashCore 0.10.5-alpha

DashCore now has a separate Geode setting, `render-pipeline-diagnostics`, default OFF. When enabled and all optional hooks are available, it collects a single 180-frame sample after `PlayLayer::setupHasCompleted()`.

The diagnostic hooks observe:
- `CCNode::visit()`: total node traversal calls.
- `CCSprite::draw()`: base CCSprite draw calls (derived classes that override draw without calling the base may not be counted here).
- `CCSpriteBatchNode::draw()`: batch draw calls.
- `CCTextureAtlas::drawQuads()` and `drawNumberOfQuads(unsigned int)`: atlas submission entry points.

Counts cover the complete 180-frame sample. Durations are measured every 8th frame. Callback timings are inclusive and nested: sprite/atlas/batch work can occur under a node traversal and draw-scene callback, so their totals overlap and must not be added together. The draw-scene CPU duration is reported separately; this instrumentation does not measure GPU execution time.

The hooks are registered with auto-enable OFF and are enabled only when the setting is explicitly requested. Missing required hooks reject the renderer profile rather than emitting a partial report. The normal renderer calls are always forwarded unchanged. This mode is diagnostic only and intentionally adds overhead while active; it does not enable culling or assert an FPS improvement.


## 11. Runtime GameObject child/sprite hierarchy census in DashCore 0.10.6-alpha

After the opt-in 180-frame render-pipeline sample finishes, DashCore performs one read-only traversal of the primary `PlayLayer::m_objects` collection. For each non-null `GameObject`, it inspects the Cocos child subtree through the confirmed `CCNode::getChildrenCount()` and `CCNode::getChildren()` APIs.

The report separates:
- Direct child count per GameObject.
- Total descendant Cocos nodes and descendant `CCSprite` instances.
- Nested GameObjects and descendant `CCSpriteBatchNode` nodes.
- Whether the object's parent chain contains a `CCSpriteBatchNode`, plus parent depth.
- Per-object subtree-size buckets and up to eight roots with the largest descendant-node counts.

Safety bounds are 4,096 descendants per GameObject, 64 levels of descendant depth, and 250,000 descendants for the complete pass. Any traversal truncation and the scan's own elapsed time are logged. The root GameObject is excluded from its descendant counters. When one GameObject is nested inside another, its descendants can appear in both per-root totals; these totals are not unique scene-node counts. Finding a batch-node ancestor is a structural clue only and does not prove which texture atlas receives a sprite.

The census is invoked once, after the 180-frame renderer sample has closed, and is not part of the measured `drawScene` interval. It does not call mutating Cocos APIs, alter visibility/transforms/z-order, submit rendering work, or modify gameplay. It is intentionally opt-in and adds a one-time CPU cost. The resulting report should guide a later targeted investigation; it does not authorize skipping `visit()`, changing batches, or claiming a performance gain.


## DashCore 0.10.7-alpha — Android section/update measurement boundary

The 2.2081 `GeometryDash.bro` map lists `addToObjectsToShow(GameObject*)` as inline and lists `reorderObjectSection(GameObject*)` / `updateAllObjectSection()` without Android addresses. The `updateObjectSection(GameObject*)` virtual entry likewise has no Android address in that map. These methods are therefore not detoured by DashCore on Android64.

Instead, the opt-in 180-frame render-pipeline sample measures the original `PlayLayer::postUpdate(float)` callback, then takes a one-shot read-only snapshot of the existing `GJBaseGameLayer` containers exposed by the 2.2081 bindings:

- `m_activeObjects.size()`, `m_activeObjectsCount`, and `m_activeObjectsIndex`.
- `m_sections` outer-slot count, each non-null row's cell count, and each non-null cell's stored pointer count.
- The number of null/empty entries, total raw pointer slots, and the maximum occupancy observed in one cell.

The census is capped at 512 outer slots and 16,384 cells. It reads only container sizes and never dereferences the `GameObject*` entries stored in section cells. Outer/inner dimensions are reported structurally; DashCore does not label them as X/Y axes without further binding/implementation evidence. The total pointer-slot count is not treated as a unique-object count.

These additions are diagnostics only. The original update callback is called exactly once, no section membership or object state is changed, and renderer/gameplay/UI behavior is untouched. The first experimental optimization remains planned for `0.11.0-alpha`, after the device-side measurements identify a safe optimization target.


## 12. Expanded outer-slot census in DashCore 0.10.8-alpha

The physical-device sample contained 824 outer entries in `m_sections`, so the previous 512-slot cap stopped before the full structure could be inspected. DashCore 0.10.8-alpha raises the one-shot read-only limits to 1,024 outer slots and 32,768 cells.

The report adds structural statistics for each scanned outer slot:
- Number of slots containing at least one cell.
- Number of fully scanned versus partially scanned slots.
- Number of fully scanned slots with one or more stored object-reference slots.
- Maximum cell slots in one outer slot.
- Maximum total raw object-reference slots in a fully scanned outer slot.

A slot whose cell list is cut short by the global cell cap is reported as partial. Its reference total is excluded from the complete-slot maximum and complete-slot count, preventing incomplete data from being presented as a full row total. Empty non-null slots may still be counted as fully scanned. Null outer entries are reported separately.

The census reads only container sizes and stored cell-vector sizes. It never dereferences the contained `GameObject*` pointers, changes section membership, or modifies object/render state. The outer/inner dimensions are kept as structural indices; they are not labelled X/Y without further evidence. Raw reference counts are not unique-object counts.

No new hooks are added for `addToObjectsToShow`, `reorderObjectSection`, `updateAllObjectSection`, or `updateObjectSection`: the 2.2081 Android binding map still does not provide verified Android hook addresses for those paths. The scan remains one-shot and opt-in, and the increase is diagnostic coverage rather than a rendering optimization.


## 13. Optional update-subphase timing in DashCore 0.10.9-alpha

The 0.10.8-alpha physical-device log showed a 6.36 ms average and 13.97 ms maximum for `PlayLayer::postUpdate`, but that callback alone does not identify which part of the gameplay update is expensive.

DashCore 0.10.9-alpha adds two optional observation points:
- `GJBaseGameLayer::update(float)`, filtered at runtime so only the active `PlayLayer` receiver is counted.
- `PlayLayer::updateVisibility(float)`, measured separately to identify the visibility callback's contribution.

These hooks are discovered through the Geode modify registry and are not prerequisites for the existing renderer profile. If Android does not expose either hook, DashCore records its readiness as false and continues with the existing measurements. No raw Android offset is invented and no section-management methods (`addToObjectsToShow`, `reorderObjectSection`, `updateAllObjectSection`, or `updateObjectSection`) are detoured.

Each callback counts calls during the 180-frame profile and times one in every eight callbacks. The callback measurements can be nested: visibility may run inside the broader base-layer update, and both can overlap other frame work. Their inclusive durations must not be summed or subtracted to claim exclusive object-update cost. The original callback is forwarded exactly once. All diagnostics remain opt-in; this release does not change gameplay, rendering, or UI and does not claim an FPS improvement.
