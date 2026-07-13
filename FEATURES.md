Quiero continuar el desarrollo de **limine-manager** y preparar la versión **1.1.0**, centrada en implementar un mecanismo seguro de rollback de snapshots Btrfs arrancados desde Limine.

## 1. Contexto obligatorio

Antes de modificar código:

1. Lee completamente `AGENTS.md`.
2. Lee `docs/PROJECT_CONTEXT.md`.
3. Lee `docs/DECISIONS.md`.
4. Revisa la implementación actual del proyecto, especialmente:

   * detección del sistema;
   * integración con Snapper;
   * modelo de snapshots;
   * `ValidationService`;
   * `ChangePlanner`;
   * `ApplyService`;
   * gestión de backups;
   * abstracciones `FileSystem` y `ProcessRunner`;
   * CLI y códigos de salida;
   * pruebas existentes.

Esos documentos y el código actual son la fuente de verdad del proyecto. No reconstruyas la arquitectura desde cero ni sustituyas decisiones existentes sin una justificación técnica clara.

Antes de implementar, resume brevemente tu comprensión de la arquitectura actual y presenta el diseño propuesto para 1.1.0. Si detectas que alguna de las decisiones descritas aquí entra en conflicto con la implementación real o con una restricción técnica de Btrfs/Snapper, detente y explícame el conflicto antes de modificar el código.

## 2. Objetivo de limine-manager 1.1.0

Actualmente limine-manager puede:

* detectar kernels;
* consultar snapshots de Snapper;
* generar entradas de Limine;
* arrancar un snapshot mediante `rootflags=subvol=@snapshots/<ID>/snapshot`;
* validar el sistema;
* mostrar `preview`, `plan`, `diff` y `dry-run`;
* aplicar `limine.conf` de forma segura;
* gestionar backups y restaurar configuraciones de Limine.

La funcionalidad de arranque de snapshots ya fue probada correctamente en un sistema real.

El escenario probado fue:

1. El sistema principal arranca desde el subvolumen Btrfs `@`.
2. Se crea un snapshot con Snapper.
3. Después del snapshot se crea `/etc/rememberme.md`.
4. Se ejecuta `sudo limine-manager apply`.
5. Se reinicia.
6. Desde Limine se arranca el snapshot anterior.
7. El sistema arranca correctamente y `/etc/rememberme.md` no existe.

El problema es que ese arranque es temporal. Al reiniciar y seleccionar la entrada normal:

`Arch Linux -> Linux`

el sistema vuelve a arrancar desde `@`, donde `/etc/rememberme.md` todavía existe.

La versión **1.1.0** debe permitir que, después de arrancar correctamente un snapshot y verificar que representa el estado deseado, el usuario pueda convertir ese estado en el nuevo sistema principal.

En otras palabras:

`boot snapshot -> verify system -> transactional rollback -> reboot -> normal Linux entry boots restored state`

## 3. Topología relevante

La instalación real utiliza:

* Arch Linux.
* Limine 12.x.
* `/boot` como ESP.
* `/boot/limine.conf`.
* Btrfs.
* Raíz principal: `@`.
* Snapshots: `@snapshots/<ID>/snapshot`.
* Snapper configurado para `/`.
* Raíz cifrada con LUKS2.
* Mapper de la raíz: `/dev/mapper/cryptroot`.
* La línea de comandos canónica está en `/etc/kernel/cmdline`.

Cuando el sistema arranca normalmente:

`rootflags=subvol=@`

Cuando arranca un snapshot generado por limine-manager:

`rootflags=subvol=@snapshots/<ID>/snapshot`

La implementación no debe asumir ciegamente que las rutas montadas visibles desde Linux son iguales a las rutas internas de los subvolúmenes Btrfs.

## 4. Requisito principal: rollback transaccional

Quiero implementar un rollback seguro del sistema principal `@` utilizando el snapshot desde el que el usuario está actualmente arrancado.

El flujo conceptual deseado es:

1. El usuario arranca desde:
   `@snapshots/<ID>/snapshot`

2. Verifica manualmente que el sistema funciona correctamente.

3. Ejecuta primero un comando no destructivo para inspeccionar el rollback.

4. limine-manager detecta:

   * filesystem Btrfs;
   * dispositivo Btrfs real;
   * subvolumen raíz actualmente arrancado;
   * ID del snapshot;
   * snapshot origen;
   * subvolumen principal `@`;
   * top-level Btrfs;
   * precondiciones necesarias.

5. Antes de sustituir `@`, preserva el estado principal actual de forma recuperable.

6. Crea un nuevo subvolumen raíz escribible basado en el snapshot seleccionado.

7. Sustituye de forma segura el `@` principal.

8. Verifica la topología resultante.

9. Regenera o actualiza `limine.conf` utilizando el flujo existente del proyecto.

10. Informa que es necesario reiniciar.

11. Después del reinicio, la entrada normal:
    `Arch Linux -> Linux`
    debe arrancar el estado restaurado.

## 5. Restricciones de seguridad

Esta funcionalidad modifica la topología Btrfs y debe tratarse como una operación de alto riesgo.

No implementes una secuencia destructiva simplista como:

`btrfs subvolume delete @`
seguido de
`btrfs subvolume snapshot ... @`

sin un diseño explícito de recuperación ante fallos.

La implementación debe:

* exigir privilegios root para la operación destructiva;
* mantener los comandos de inspección y planificación sin privilegios cuando sea posible;
* negarse a ejecutar el rollback si las precondiciones no son seguras;
* detectar de forma inequívoca el subvolumen actualmente arrancado;
* comprobar que el sistema está arrancado desde un snapshot administrado por Snapper;
* comprobar que el snapshot corresponde al filesystem raíz esperado;
* comprobar que el `@` principal no es el subvolumen raíz actualmente montado;
* impedir el rollback desde el arranque normal de `@` en la primera versión;
* acceder al top-level Btrfs de forma controlada, probablemente mediante `subvolid=5`;
* utilizar un directorio temporal privado bajo `/run/limine-manager/` si es necesario montar el top-level;
* limpiar mounts y recursos temporales incluso ante errores;
* protegerse contra ejecuciones concurrentes;
* preservar el estado anterior de `@` antes de sustituirlo;
* definir claramente qué ocurre si falla cada fase;
* evitar dejar el sistema sin un subvolumen principal `@`;
* verificar el resultado antes de considerar la operación exitosa;
* no eliminar automáticamente el estado anterior hasta que exista una política de retención explícita;
* no ejecutar automáticamente esta operación desde el hook de pacman;
* no cambiar silenciosamente la semántica de `apply` o `restore`, que actualmente gestionan `limine.conf`, no snapshots Btrfs.

Si una operación Btrfs no puede hacerse de forma realmente atómica, documenta con precisión el límite de atomicidad y diseña una estrategia de recuperación por fases.

## 6. Arquitectura propuesta

Evalúa una arquitectura similar a:

`domain/`

* `rollback_plan`

`application/`

* `RollbackPlanner`
* `RollbackService`

`infrastructure/`

* `BtrfsClient`

No es obligatorio conservar exactamente estos nombres si la arquitectura actual sugiere algo mejor, pero quiero mantener la separación entre:

* detección;
* planificación;
* ejecución;
* comandos externos;
* filesystem;
* renderizado de salida.

`RollbackService` no debería contener parsing improvisado de `findmnt`, `btrfs` o Snapper. Esa responsabilidad debe estar encapsulada en infraestructura y representada mediante modelos tipados.

Reutiliza `FileSystem` y `ProcessRunner` cuando tenga sentido. No introduzcas una segunda abstracción redundante para ejecutar procesos.

## 7. CLI deseada

Diseña, como mínimo, comandos equivalentes a:

`limine-manager rollback-status`

Muestra si el sistema está arrancado desde:

* `@`;
* un snapshot administrado;
* un subvolumen desconocido.

Debe mostrar también, cuando sea posible:

* snapshot ID;
* subvolumen actual;
* subvolumen principal;
* dispositivo Btrfs;
* si el rollback es elegible.

`limine-manager rollback-plan`

Debe ser completamente no destructivo y mostrar exactamente qué operaciones serían necesarias.

Ejemplo conceptual:

Rollback plan

Source snapshot:
123

Current booted root:
@snapshots/123/snapshot

Target root:
@

Current @ will be preserved before replacement.

Operations:

1. Validate rollback preconditions
2. Mount Btrfs top-level
3. Preserve current @
4. Create writable replacement from snapshot 123
5. Replace the main @
6. Verify resulting Btrfs topology
7. Regenerate limine.conf
8. Require reboot

Y:

`sudo limine-manager rollback`

Debe ejecutar únicamente un plan previamente validado por la misma lógica utilizada por `rollback-plan`.

No quiero que `rollback-plan` y `rollback` implementen dos algoritmos independientes.

Si consideras necesaria una confirmación interactiva, analiza primero cómo afectaría a automatización, pruebas y consistencia con el CLI actual. Prefiero una opción explícita antes que depender exclusivamente de interacción por stdin.

## 8. Preservación del sistema anterior

Antes de reemplazar `@`, el estado actual debe conservarse.

Diseña una estrategia clara para decidir si:

* se renombra el antiguo `@`;
* se crea un snapshot de recuperación;
* se integra con Snapper;
* o se utiliza otra estrategia Btrfs más segura.

La recuperación debe tener un identificador inequívoco y no debe colisionar con nombres existentes.

Debes analizar especialmente la diferencia entre:

* preservar el subvolumen `@` actual;
* crear un snapshot Snapper válido;
* simplemente renombrar un subvolumen;
* mantener coherencia con la configuración de Snapper.

No asumas que esas operaciones son equivalentes.

## 9. Snapshots read-only

Los snapshots de Snapper normalmente pueden ser read-only.

El nuevo `@` principal debe ser escribible.

La implementación debe comprobar explícitamente las propiedades relevantes del snapshot origen y crear el nuevo subvolumen con la semántica correcta.

No modifiques el snapshot histórico original para convertirlo en `@`.

## 10. Recuperación ante fallos

Antes de escribir código, define una máquina de estados o secuencia de fases.

Por ejemplo:

* validated;
* top-level mounted;
* current root preserved;
* replacement created;
* target switched;
* topology verified;
* Limine regenerated;
* completed.

Para cada fase, documenta:

* qué recursos existen;
* qué cambios ya son persistentes;
* qué rollback automático es posible;
* qué recuperación manual sería necesaria si el proceso termina abruptamente.

Considera también interrupciones por:

* excepción;
* señal;
* corte de energía;
* filesystem lleno;
* fallo de `btrfs subvolume snapshot`;
* fallo de rename;
* fallo al regenerar `limine.conf`.

No afirmes atomicidad total si Btrfs y el filesystem no pueden garantizarla para toda la transacción.

## 11. Pruebas obligatorias

No dependas del sistema real para las pruebas normales.

Añade pruebas para, como mínimo:

* sistema arrancado normalmente desde `@`;
* sistema arrancado desde `@snapshots/123/snapshot`;
* snapshot desconocido;
* snapshot inexistente;
* snapshot no perteneciente a la configuración Snapper esperada;
* `@` principal inexistente;
* conflicto con un nombre de recuperación;
* snapshot read-only;
* creación de nuevo `@` escribible;
* fallo antes de preservar `@`;
* fallo después de preservar `@`;
* fallo durante la creación del reemplazo;
* fallo durante el cambio final;
* fallo al verificar;
* fallo al regenerar Limine;
* limpieza del mount temporal;
* ejecución concurrente;
* `rollback-plan` sin escrituras;
* rechazo del rollback cuando la raíz activa es `@`.

Utiliza dobles de prueba para `ProcessRunner` y las abstracciones existentes.

Si para probar correctamente Btrfs real hacen falta tests privilegiados o loop devices, sepáralos como pruebas de integración opt-in y no hagas que el `ctest` normal requiera root.

## 12. Compatibilidad

La versión será **1.1.0**.

Debe conservar:

* esquema de configuración versión 1, salvo que exista una razón técnica imprescindible para ampliarlo de forma compatible;
* comandos existentes;
* códigos de salida existentes;
* comportamiento de `apply`;
* comportamiento de `restore` de `limine.conf`;
* formato actual generado de `limine.conf`, salvo cambios estrictamente necesarios;
* compatibilidad con la configuración 1.0.x.

No conviertas esta funcionalidad en un breaking change.

## 13. Documentación

Actualiza como mínimo:

* `CHANGELOG.md`;
* `README.md`;
* `docs/PROJECT_CONTEXT.md`;
* `docs/DECISIONS.md`;
* documentación man;
* completions de Bash y Zsh;
* release notes para `1.1.0`.

Documenta claramente la diferencia entre:

* arrancar un snapshot;
* restaurar `limine.conf`;
* hacer rollback del sistema Btrfs.

El comando existente `restore` restaura una copia de `limine.conf`; no debe confundirse con el nuevo rollback del sistema.

## 14. Forma de trabajo

No escribas toda la implementación inmediatamente.

Trabaja en este orden:

1. Lee el contexto y el código.
2. Explica la arquitectura actual relevante.
3. Investiga las garantías y limitaciones reales de las operaciones Btrfs que planeas utilizar.
4. Propón el diseño de 1.1.0.
5. Define invariantes y modelo de recuperación.
6. Define cambios de CLI.
7. Define pruebas.
8. Solo después comienza la implementación.
9. Compila en Debug y Release.
10. Ejecuta todas las pruebas.
11. Ejecuta sanitizers si están configurados en el proyecto.
12. Ejecuta `format-check`.
13. No declares la versión terminada si alguna comprobación obligatoria falla.

## 15. Criterio de aceptación principal

La prueba real final será:

1. Arrancar normalmente desde `@`.
2. Crear un snapshot con Snapper.
3. Modificar el sistema, por ejemplo creando `/etc/rememberme.md`.
4. Regenerar `limine.conf`.
5. Reiniciar.
6. Arrancar el snapshot anterior desde Limine.
7. Confirmar que `/etc/rememberme.md` no existe.
8. Ejecutar `limine-manager rollback-status`.
9. Ejecutar `limine-manager rollback-plan`.
10. Ejecutar `sudo limine-manager rollback`.
11. Reiniciar.
12. Seleccionar `Arch Linux -> Linux`.
13. Confirmar que el sistema arranca normalmente desde `@`.
14. Confirmar que `/etc/rememberme.md` sigue sin existir.
15. Confirmar que el antiguo estado de `@` quedó preservado y es recuperable.

Primero analiza y presenta el diseño. No implementes la operación destructiva hasta que el diseño, las invariantes y la estrategia de recuperación estén claramente definidos.

