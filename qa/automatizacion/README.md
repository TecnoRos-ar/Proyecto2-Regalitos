# Pruebas automáticas de Regalitos Argentinos

Pruebas de extremo a extremo con [Playwright](https://playwright.dev) sobre
`Regalitos Argentinos.html`. Los ID de las pruebas (`REG-…`) son los del plan de pruebas.

## Estructura

| Carpeta o archivo | Contenido |
|---|---|
| `tests/` | Las pruebas, un archivo por área: arranque, materia prima, pulseras, stock, guardado, borrado, interfaz |
| `pages/` | Objetos de página: lo único que conoce los selectores de la pantalla |
| `fixtures/` | Preparación común: abre la página, limita la red y carga los datos base |
| `data/` | Datos de prueba del plan (DS-01, DS-02) con sus resultados calculados a mano |
| `scripts/informe.mjs` | Escribe el registro de la ejecución en `../ejecuciones/` |
| `playwright.config.ts` | Proyectos `escritorio` (1366 × 768) y `movil` (390 × 844), informes, trazas |
| `correr.sh` | Corre todo dentro de un entorno aislado |

## Cómo se usa

Requisitos: Linux con `bwrap`, Node 20 o superior y Google Chrome instalado.

```bash
./correr.sh instalar          # una vez
./correr.sh                   # todas las pruebas
./correr.sh --grep @P0        # solo prioridad alta
./correr.sh --project=movil   # solo móvil
./correr.sh tests/stock.spec.ts
```

Sin entorno aislado: `npm ci && npx playwright test`.

Para probar otro archivo: `REGALITOS_HTML=/ruta/al/archivo.html ./correr.sh`.

## Resultados
- `../ejecuciones/21.81-evidencia-AAAA-MM-DD-playwright.md`: resumen de la corrida.
- `playwright-report/`: informe HTML (`npm run reporte`).
- `test-results/`: captura y traza de cada prueba que falla.

## Reglas para agregar pruebas
- El título empieza con el ID del plan y termina con la prioridad: `REG-STK-01 … @P0`.
- Una prueba no depende de otra: cada una empieza con la página vacía o con el fixture
  `taller` (DS-01).
- Los selectores van en `pages/`, nunca en la prueba. Se prefiere lo que ve el usuario
  (rol, etiqueta, texto) antes que atributos internos.
- El resultado esperado se calcula a mano y se escribe en la prueba o en `data/`.
- Sin esperas fijas: se usan las aserciones de Playwright, que reintentan solas.
- La página solo puede pedir los dominios listados en `fixtures/regalitos.ts`; otro
  dominio hace fallar la prueba.
