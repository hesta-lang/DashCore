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
