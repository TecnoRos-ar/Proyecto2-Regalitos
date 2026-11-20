# Regalitos Argentinos

Página para calcular costos, precio de venta y stock de materia prima de un taller de
pulseras. Es un solo archivo: `Regalitos Argentinos.html`. No tiene servidor, instalación
ni dependencias.

## Cómo se usa
1. Abrir `Regalitos Argentinos.html` en un navegador actual (Chrome o Firefox).
2. En **Materia prima**, cargar cada material: en qué se mide, el stock, cuánto se pagó y
   por qué cantidad.
3. En **Pulseras**, crear un modelo y armar su receta. La página calcula el costo, el
   precio de venta y cuántas pulseras alcanza a armar el stock.
4. Al armar pulseras, indicar cuántas y pulsar **Descontar del stock**.

## Cómo se escriben los números
- Los decimales se pueden escribir con coma o con punto: `0,5` y `0.5` valen lo mismo.
- Si el número lleva coma, los puntos se leen como separadores de miles: `1.234,5` es
  1234,5.
- Si no lleva coma, el punto es decimal: `1.234` es 1,234, no mil doscientos treinta y
  cuatro. Para escribir miles, no usar punto o agregar la coma: `1234` o `1.234,0`.
- La página muestra los valores con coma decimal y sin separador de miles.
- Un valor negativo o que no es un número pasa a 0.
- "Armé (pulseras)" solo acepta un número entero, de 1 en adelante.

## Reglas de cálculo
- Costo unitario del material = lo pagado / la cantidad comprada.
- Costo de la pulsera = materiales de la receta + mano de obra + packaging y otros.
- Precio de venta = costo + ganancia (porcentaje sobre el costo o monto fijo), redondeado
  hacia arriba al múltiplo elegido.
- El porcentaje que se muestra junto a la ganancia es sobre el precio de venta.
- Capacidad = la menor cantidad de pulseras que permite cada material de la receta.

## Dónde se guardan los datos
- Abierta como archivo, la página guarda en el almacenamiento del navegador
  (`localStorage`, clave `regalitos-argentinos-v1`). Los datos quedan solo en ese
  navegador y en ese dispositivo.
- Borrar los datos del sitio en el navegador borra el taller. No hay exportación ni copia
  de seguridad.
- Si el entorno ofrece una base de datos (`window.claude.use('db')`), la página guarda
  ahí en lugar del navegador.

## Red
La página pide dos fuentes a Google Fonts. Sin conexión funciona igual, con la fuente del
sistema.

## Pruebas
La carpeta `qa/` tiene el trabajo de QA:

- `qa/21.81-plan-de-pruebas.md`: plan, datos de prueba e inventario de casos.
- `qa/21.81-casos-p0.md`, `qa/21.81-casos-p1.md` y `qa/21.81-casos-p2.md`: los 45 casos, con pasos y resultado esperado.
- `qa/21.81-hallazgos.md`: registro de defectos y dudas, con su estado.
- `qa/ejecuciones/`: evidencia de cada corrida.
- `qa/automatizacion/`: pruebas automáticas con Playwright. Ver su `README.md`.

```bash
cd qa/automatizacion
./correr.sh instalar   # una vez
./correr.sh            # corre todas las pruebas en un entorno aislado
```

Requiere Linux con `bwrap`, Node 20 o superior y Google Chrome.

## Pendiente de definir
- Alcance funcional y criterios de aceptación.
- Copia de seguridad o exportación de los datos.
- Stock de pulseras terminadas e historial de producción.
