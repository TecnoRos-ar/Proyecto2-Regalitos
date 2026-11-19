import { expect, test } from '../fixtures/regalitos';
import { hilo } from '../data/ds01';

test.describe('Descuento de stock', () => {
  test('REG-STK-01 armar 3 descuenta los materiales (DS-03) @P0', async ({ taller }) => {
    const clasica = taller.pulsera();
    await clasica.armar(3);
    await expect(clasica.mensaje).toHaveText('Listo. Descontamos del stock los materiales de 3 pulseras.');
    await expect(clasica.capacidad).toHaveText('Con tu stock podés armar 7 pulseras (el que limita: Mostacillas).');
    expect(await taller.stocks()).toEqual(['48.5', '140', '7']);
    await expect(taller.valorTotalDelStock).toHaveText('$ 5.005');
  });

  test('REG-STK-02 armar más de lo que alcanza no descuenta @P0', async ({ taller }) => {
    const clasica = taller.pulsera();
    await clasica.armar(11);
    await expect(clasica.mensaje).toHaveText('No alcanza el stock. Mostacillas: te faltan 20 un · Cierre: te faltan 1 un');
    expect(await taller.stocks()).toEqual(['50', '200', '10']);
  });

  test('REG-STK-03 armar 0, vacío, negativo o 1,9 no descuenta @P0', async ({ taller }) => {
    const clasica = taller.pulsera();
    for (const cantidad of ['0', '', '-2']) {
      await clasica.armar(cantidad);
      await expect(clasica.mensaje).toHaveText('Poné cuántas pulseras armaste.');
    }
    await clasica.armar('1.9');
    await expect(clasica.mensaje).toHaveText('Poné un número entero de pulseras.');
    expect(await taller.stocks()).toEqual(['50', '200', '10']);
  });

  test('REG-STK-04 cantidades con tres decimales no pierden precisión @P1', async ({ regalitos }) => {
    await regalitos.agregarMaterial({ ...hilo, nombre: 'Hilo', stock: 1 });
    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [{ material: 'Hilo', cantidad: 0.335 }] });
    await pulsera.armar(1);
    expect(await regalitos.stocks()).toEqual(['0.665']);
  });
});
