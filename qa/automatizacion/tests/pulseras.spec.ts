import { expect, test } from '../fixtures/regalitos';
import { hilo, variantesDePrecio } from '../data/ds01';

test.describe('Pulseras', () => {
  test('REG-PUL-01 alta de "Clásica" con DS-01 @P0 @movil', async ({ taller }) => {
    const clasica = taller.pulsera();
    await expect(clasica.materiales).toHaveText('$ 265,00');
    await expect(clasica.extras).toHaveText('$ 135,00');
    await expect(clasica.costo).toHaveText('$ 400,00');
    await expect(clasica.precio).toHaveText('$ 800,00');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 400,00 · 50% del precio');
    await expect(clasica.capacidad).toHaveText('Con tu stock podés armar 10 pulseras (el que limita: Mostacillas).');
  });

  test('REG-PUL-02 el mismo material dos veces en la receta se suma @P0', async ({ regalitos }) => {
    await regalitos.agregarMaterial({ ...hilo, nombre: 'Hilo', stock: 1.5 });
    const pulsera = await regalitos.agregarPulsera({
      nombre: 'P1',
      receta: [{ material: 'Hilo', cantidad: 1 }, { material: 'Hilo', cantidad: 1 }],
    });
    await expect(pulsera.capacidad).toHaveText('Con tu stock no alcanza para armar una (te falta: Hilo).');
    await pulsera.armar(1);
    await expect(pulsera.mensaje).toHaveText('No alcanza el stock. Hilo: te faltan 0,5 m');
    expect(await regalitos.stocks()).toEqual([1.5]);
  });

  test('REG-PUL-03 no acepta una cantidad de receta negativa @P0', async ({ regalitos }) => {
    await regalitos.agregarMaterial({ ...hilo, nombre: 'Hilo', stock: 10 });
    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [{ material: 'Hilo', cantidad: -1 }] });
    await expect(pulsera.filas.first().getByLabel('Cantidad')).toHaveValue('0');
    await expect(pulsera.costo).toHaveText('$ 0,00');
    await pulsera.armar(1);
    expect(await regalitos.stocks()).toEqual([10]);
  });

  test('REG-PUL-04 una pulsera sin receta calcula su costo con los extras @P1', async ({ regalitos }) => {
    const sola = await regalitos.agregarPulsera({ nombre: 'Sola', receta: [], manoObra: 100, otros: 35 });
    await expect(sola.materiales).toHaveText('$ 0,00');
    await expect(sola.extras).toHaveText('$ 135,00');
    await expect(sola.costo).toHaveText('$ 135,00');
    await expect(sola.precio).toHaveText('$ 300,00');
    await expect(sola.ganancia).toHaveText('Ganancia $ 165,00 · 55% del precio');
    await expect(sola.capacidad).toHaveText('Agregá materiales para ver cuántas podés armar con tu stock.');
    /* "Descontar del stock" sin receta: sin esperado definido (REG-HAL-21). No se afirma. */
  });

  test('REG-PUL-06 cambiar el material de una fila cambia unidad, subtotal y totales @P1', async ({ taller }) => {
    const clasica = taller.pulsera();
    const opcion = (texto: string) => clasica.opcionesDeMaterial(0).filter({ hasText: texto }).getAttribute('value');
    await clasica.materialDeFila(0).selectOption((await opcion('Mostacillas'))!);
    await expect(clasica.unidadDeFila(0)).toHaveText('un');
    await expect(clasica.subtotalDeFila(0)).toHaveText('$ 2,00');
    await expect(clasica.materiales).toHaveText('$ 232,00');
    await expect(clasica.costo).toHaveText('$ 367,00');
    await expect(clasica.precio).toHaveText('$ 800,00');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 433,00 · 54,13% del precio');
    /* Las dos filas de Mostacillas suman 20,5: 200 / 20,5 = 9. */
    await expect(clasica.capacidad).toHaveText('Con tu stock podés armar 9 pulseras (el que limita: Mostacillas).');

    await clasica.materialDeFila(0).selectOption((await opcion('Hilo encerado'))!);
    await expect(clasica.subtotalDeFila(0)).toHaveText('$ 35,00');
    await expect(clasica.costo).toHaveText('$ 400,00');
    await expect(clasica.capacidad).toHaveText('Con tu stock podés armar 10 pulseras (el que limita: Mostacillas).');
  });

  test('REG-PUL-07 quitar una fila recalcula costo, precio y capacidad @P1', async ({ taller, page }) => {
    const clasica = taller.pulsera();
    await clasica.quitarFila(2).click();
    await expect(clasica.filas).toHaveCount(2);
    await expect(clasica.materiales).toHaveText('$ 115,00');
    await expect(clasica.costo).toHaveText('$ 250,00');
    await expect(clasica.precio).toHaveText('$ 500,00');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 250,00 · 50% del precio');
    await expect(clasica.capacidad).toHaveText('Con tu stock podés armar 10 pulseras (el que limita: Mostacillas).');
    await taller.esperarGuardado();
    await page.reload();
    await expect(taller.pulsera().filas).toHaveCount(2);
  });

  test('REG-PUL-08 cambiar el precio de compra recalcula las pulseras @P1', async ({ taller }) => {
    await taller.irAMateriaPrima();
    const hiloEncerado = taller.material(0);
    await hiloEncerado.pague.fill('7000');
    await expect(hiloEncerado.costoUnitario).toHaveText('$ 140,00 / m');
    await expect(hiloEncerado.valorDelStock).toHaveText('$ 7.000');
    await expect(taller.valorTotalDelStock).toHaveText('$ 9.300');

    await taller.irAPulseras();
    const clasica = taller.pulsera();
    await expect(clasica.materiales).toHaveText('$ 300,00');
    await expect(clasica.costo).toHaveText('$ 435,00');
    await expect(clasica.precio).toHaveText('$ 900,00');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 465,00 · 51,67% del precio');
    await expect(clasica.capacidad).toHaveText('Con tu stock podés armar 10 pulseras (el que limita: Mostacillas).');
  });

  test('REG-PUL-09 dos pulseras pueden llevar el mismo nombre @P2', async ({ taller, page }) => {
    /* Comportamiento actual, leído del código. Falta que el equipo confirme si se permite. */
    const segunda = await taller.agregarPulsera({ nombre: 'Clásica', receta: [], manoObra: 500 });
    await expect(taller.tarjetas).toHaveCount(2);
    await expect(segunda.costo).toHaveText('$ 500,00');
    await expect(taller.pulsera(0).costo).toHaveText('$ 400,00');

    await taller.esperarGuardado();
    await page.reload();
    await expect(taller.pulsera(0).nombre).toHaveValue('Clásica');
    await expect(taller.pulsera(1).nombre).toHaveValue('Clásica');
    await expect(taller.pulsera(1).costo).toHaveText('$ 500,00');

    await taller.pulsera(1).botonBorrar.click();
    await taller.pulsera(1).botonBorrar.click();
    await expect(taller.tarjetas).toHaveCount(1);
    await expect(taller.pulsera(0).costo).toHaveText('$ 400,00');
  });

  test('REG-PUL-05 sin materia prima no se puede armar la receta @P1', async ({ regalitos }) => {
    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [] });
    await expect(pulsera.raiz.getByRole('button', { name: '+ Agregar material' })).toBeDisabled();
    await expect(pulsera.raiz.getByText('Primero cargá materia prima.')).toBeVisible();
  });
});

test.describe('Precio de venta', () => {
  for (const v of variantesDePrecio) {
    const nombre = `${v.tipo === 'pct' ? `${v.valor} %` : `$ ${v.valor} fija`}, redondeo ${v.redondeo}`;
    test(`REG-PRE-01 ${nombre} da ${v.precio} @P0`, async ({ taller }) => {
      const clasica = taller.pulsera();
      await clasica.tipoDeGanancia.selectOption(v.tipo);
      await clasica.valorDeGanancia.fill(String(v.valor));
      await taller.redondeo.selectOption(String(v.redondeo));
      await expect(clasica.precio).toHaveText(v.precio);
      await expect(clasica.ganancia).toHaveText(v.ganancia);
    });
  }

  test('REG-PRE-03 la ganancia sugerida vale para las pulseras nuevas y no cambia las existentes @P1', async ({ taller, page }) => {
    await taller.gananciaSugerida.fill('50');
    const clasica = taller.pulsera(0);
    await expect(clasica.valorDeGanancia).toHaveValue('100');
    await expect(clasica.precio).toHaveText('$ 800,00');

    const nueva = await taller.agregarPulsera({ nombre: 'Nueva', receta: [] });
    await expect(nueva.tipoDeGanancia).toHaveValue('pct');
    await expect(nueva.valorDeGanancia).toHaveValue('50');

    await taller.esperarGuardado();
    await page.reload();
    await expect(taller.gananciaSugerida).toHaveValue('50');
  });

  test('REG-PRE-04 cambiar de % a $ fija conserva el número @P1', async ({ taller }) => {
    /* Comportamiento actual, leído del código. Falta que el equipo confirme si es el esperado. */
    const clasica = taller.pulsera();
    await clasica.tipoDeGanancia.selectOption('fija');
    await expect(clasica.rotulo('Monto ($)')).toBeVisible();
    await expect(clasica.valorDeGanancia).toHaveValue('100');
    await expect(clasica.precio).toHaveText('$ 500,00');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 100,00 · 20% del precio');

    await clasica.tipoDeGanancia.selectOption('pct');
    await expect(clasica.valorDeGanancia).toHaveValue('100');
    await expect(clasica.precio).toHaveText('$ 800,00');
  });

  test('REG-PRE-05 la ganancia se carga sobre el costo y se muestra sobre el precio (REG-HAL-13) @P1', async ({ taller }) => {
    /* Comportamiento actual. Falta que el equipo confirme qué porcentaje quiere mostrar. */
    const clasica = taller.pulsera();
    await expect(clasica.valorDeGanancia).toHaveValue('100');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 400,00 · 50% del precio');
    await clasica.valorDeGanancia.fill('50');
    await expect(clasica.precio).toHaveText('$ 600,00');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 200,00 · 33,33% del precio');
  });

  test('REG-PRE-02 cambiar el redondeo recalcula todas las pulseras @P0', async ({ taller, page }) => {
    /* "Simple": un Cierre, sin extras, 100 % sobre el costo. Costo $ 150,00. */
    const simple = await taller.agregarPulsera({ nombre: 'Simple', receta: [{ material: 'Cierre', cantidad: 1 }] });
    const clasica = taller.pulsera(0);
    await expect(clasica.precio).toHaveText('$ 800,00');
    await expect(simple.precio).toHaveText('$ 300,00');

    await taller.redondeo.selectOption('500');
    await expect(clasica.precio).toHaveText('$ 1.000');
    await expect(clasica.ganancia).toHaveText('Ganancia $ 600,00 · 60% del precio');
    await expect(simple.precio).toHaveText('$ 500,00');
    await expect(simple.ganancia).toHaveText('Ganancia $ 350,00 · 70% del precio');

    await taller.redondeo.selectOption('0');
    await expect(clasica.precio).toHaveText('$ 800,00');
    await expect(simple.precio).toHaveText('$ 300,00');

    await taller.redondeo.selectOption('500');
    await expect(taller.estado).toHaveText('Guardado en este navegador');
    await page.reload();
    await expect(taller.redondeo).toHaveValue('500');
    await expect(taller.pulsera(0).precio).toHaveText('$ 1.000');
    await expect(taller.pulsera(1).precio).toHaveText('$ 500,00');
  });
});
