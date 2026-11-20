import { expect, test } from '../fixtures/regalitos';
import { cierre, hilo, mostacillas } from '../data/ds01';

test.describe('Materia prima', () => {
  test('REG-MAT-01 alta de los tres materiales de DS-01 @P0', async ({ regalitos }) => {
    await regalitos.agregarMaterial(hilo);
    await regalitos.agregarMaterial(mostacillas);
    await regalitos.agregarMaterial(cierre);

    await expect(regalitos.material(0).costoUnitario).toHaveText('$ 70,00 / m');
    await expect(regalitos.material(0).valorDelStock).toHaveText('$ 3.500');
    await expect(regalitos.material(1).costoUnitario).toHaveText('$ 4,00 / un');
    await expect(regalitos.material(1).valorDelStock).toHaveText('$ 800,00');
    await expect(regalitos.material(2).costoUnitario).toHaveText('$ 150,00 / un');
    await expect(regalitos.material(2).valorDelStock).toHaveText('$ 1.500');
    await expect(regalitos.valorTotalDelStock).toHaveText('$ 5.800');
  });

  test('REG-MAT-02 cambiar "Se mide en" cambia los textos y no el costo @P0', async ({ taller }) => {
    await taller.irAMateriaPrima();
    const hiloEncerado = taller.material(0);
    await hiloEncerado.unidad.selectOption('g');
    await expect(hiloEncerado.campo('Stock actual (gramos)')).toBeVisible();
    await expect(hiloEncerado.campo('por (gramos)')).toBeVisible();
    await expect(hiloEncerado.rotulo('Costo por gramo')).toBeVisible();
    await expect(hiloEncerado.costoUnitario).toHaveText('$ 70,00 / g');
    await expect(hiloEncerado.valorDelStock).toHaveText('$ 3.500');

    await taller.irAPulseras();
    const clasica = taller.pulsera();
    await expect(clasica.opcionesDeMaterial(0)).toHaveText(['Hilo encerado · g', 'Mostacillas · un', 'Cierre · un']);
    await expect(clasica.costo).toHaveText('$ 400,00');
    await expect(clasica.precio).toHaveText('$ 800,00');

    await taller.irAMateriaPrima();
    await taller.material(0).unidad.selectOption('m');
    await expect(taller.material(0).campo('Stock actual (metros)')).toBeVisible();
    await expect(taller.material(0).costoUnitario).toHaveText('$ 70,00 / m');
  });

  test('REG-MAT-03 "por" en 0 avisa que falta la cantidad comprada @P0', async ({ regalitos }) => {
    const material = await regalitos.agregarMaterial({ ...hilo, por: 0 });
    await expect(material.costoUnitario).toHaveText('Falta la cantidad comprada');
  });

  test('REG-MAT-04 no acepta stock, "Pagué" ni "por" negativos @P0', async ({ regalitos }) => {
    const material = await regalitos.agregarMaterial({ ...hilo, stock: -5, pague: -100, por: -2 });
    await expect(material.stock).toHaveValue('0');
    await expect(material.pague).toHaveValue('0');
    await expect(material.por).toHaveValue('0');
  });

  test('REG-MAT-05 un material sin nombre se identifica en la receta y en los mensajes @P1', async ({ regalitos }) => {
    const material = await regalitos.agregarMaterial({ nombre: '', unidad: 'un', stock: 10, pague: 100, por: 10 });
    await expect(material.costoUnitario).toHaveText('$ 10,00 / un');
    await expect(material.valorDelStock).toHaveText('$ 100,00');

    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [{ material: '(sin nombre)', cantidad: 1 }] });
    await expect(pulsera.opcionesDeMaterial(0)).toHaveText(['(sin nombre) · un']);
    await expect(pulsera.capacidad).toHaveText('Con tu stock podés armar 10 pulseras (el que limita: material sin nombre).');
    await pulsera.armar(11);
    await expect(pulsera.mensaje).toHaveText('No alcanza el stock. Material sin nombre: te faltan 1 un');
  });

  test('REG-MAT-07 la coma y el punto decimal dan el mismo valor (REG-HAL-19) @P1', async ({ regalitos }) => {
    const material = await regalitos.agregarMaterial({ nombre: 'Hilo', unidad: 'm', stock: 0, pague: 100, por: 10 });
    await material.stock.fill('');
    await material.stock.pressSequentially('0.5');
    await expect(material.valorDelStock).toHaveText('$ 5,00');
    await material.stock.fill('');
    await material.stock.pressSequentially('0,5');
    await expect(material.valorDelStock, 'valor del stock tras escribir "0,5"').toHaveText('$ 5,00');
  });

  test('REG-MAT-08 un importe muy grande no rompe la pantalla @P1 @movil', async ({ regalitos }) => {
    const material = await regalitos.agregarMaterial({ nombre: 'Grande', unidad: 'un', stock: 999999999, pague: 999999999, por: 1 });
    await expect(material.costoUnitario).toHaveText('$ 999.999.999 / un');
    /* 999999999² no entra exacto en un número de JavaScript: se acepta la diferencia de precisión. */
    await expect(material.valorDelStock).toHaveText('$ 999.999.998.000.000.000');
    expect(await regalitos.desbordeHorizontal(), 'píxeles que sobresalen del ancho de la pantalla').toBeLessThanOrEqual(0);
  });

  test('REG-MAT-09 un nombre con HTML, comillas y emojis se muestra como texto @P2', async ({ regalitos, page }) => {
    const raro = `<b>Hilo</b> "doble" 'simple' & 🧵`;
    const material = await regalitos.agregarMaterial({ nombre: raro, unidad: 'm', stock: 1, pague: 100, por: 10 });
    await expect(material.nombre).toHaveValue(raro);

    const pulsera = await regalitos.agregarPulsera({ nombre: raro, receta: [{ material: 'Hilo', cantidad: 2 }] });
    await expect(pulsera.opcionesDeMaterial(0)).toHaveText([`${raro} · m`]);
    await expect(pulsera.capacidad).toHaveText(`Con tu stock no alcanza para armar una (te falta: ${raro}).`);
    await pulsera.armar(1);
    await expect(pulsera.mensaje).toHaveText(`No alcanza el stock. ${raro}: te faltan 1 m`);
    await expect(regalitos.negritasEnTarjetas).toHaveCount(0);

    await regalitos.esperarGuardado();
    await page.reload();
    await expect(regalitos.pulsera().nombre).toHaveValue(raro);
    await expect(regalitos.pulsera().opcionesDeMaterial(0)).toHaveText([`${raro} · m`]);
    await expect(regalitos.negritasEnTarjetas).toHaveCount(0);
  });

  test('REG-MAT-10 un nombre de 200 caracteres no rompe el diseño (REG-HAL-22) @P2 @movil', async ({ regalitos }) => {
    const largo = 'A'.repeat(200);
    const material = await regalitos.agregarMaterial({ nombre: largo, unidad: 'm', stock: 1, pague: 100, por: 10 });
    await expect(material.nombre).toHaveValue(largo);
    expect.soft(await regalitos.desbordeHorizontal(), 'píxeles que sobresalen en "Materia prima"').toBeLessThanOrEqual(0);

    await regalitos.agregarPulsera({ nombre: 'P1', receta: [{ material: largo, cantidad: 2 }] });
    expect.soft(await regalitos.desbordeHorizontal(), 'píxeles que sobresalen en "Pulseras"').toBeLessThanOrEqual(0);
  });

  test('REG-MAT-06 un material sin stock lo indica @P1', async ({ regalitos, page }) => {
    await regalitos.agregarMaterial({ ...hilo, stock: 0 });
    await expect(page.getByText('Sin stock')).toBeVisible();
  });
});
