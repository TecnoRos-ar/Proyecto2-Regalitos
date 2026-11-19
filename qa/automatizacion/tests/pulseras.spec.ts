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
    expect(await regalitos.stocks()).toEqual(['1.5']);
  });

  test('REG-PUL-03 no acepta una cantidad de receta negativa @P0', async ({ regalitos }) => {
    await regalitos.agregarMaterial({ ...hilo, nombre: 'Hilo', stock: 10 });
    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [{ material: 'Hilo', cantidad: -1 }] });
    await expect(pulsera.filas.first().getByLabel('Cantidad')).toHaveValue('0');
    await expect(pulsera.costo).toHaveText('$ 0,00');
    await pulsera.armar(1);
    expect(await regalitos.stocks()).toEqual(['10']);
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
