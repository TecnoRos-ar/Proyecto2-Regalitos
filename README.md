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

## Pendiente de definir
- Alcance funcional y criterios de aceptación.
- Copia de seguridad o exportación de los datos.
- Stock de pulseras terminadas e historial de producción.
