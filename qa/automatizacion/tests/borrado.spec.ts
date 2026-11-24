import { expect, test } from '../fixtures/regalitos';
import { hilo } from '../data/ds01';

test.describe('Borrado', () => {
  test('REG-BOR-01 avisa en cuántas pulseras se usa el material @P0', async ({ regalitos }) => {
    await regalitos.agregarMaterial({ ...hilo, nombre: 'Hilo' });
    await regalitos.agregarPulsera({ nombre: 'P1', receta: [{ material: 'Hilo', cantidad: 1 }] });
    await regalitos.irAMateriaPrima();
    const material = regalitos.material(0);
    await material.botonBorrar.click();
    await expect(material.botonBorrar).toHaveText('Se usa en 1 pulsera. Tocá otra vez para borrar');
  });

  test('REG-BOR-02 el borrado pide dos toques y se desarma a los 3 s @P0', async ({ regalitos }) => {
    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [] });
    await pulsera.botonBorrar.click();
    await expect(pulsera.botonBorrar).toHaveText('¿Seguro? Tocá otra vez');
    await expect(pulsera.botonBorrar).toHaveText('Borrar pulsera', { timeout: 4_000 });
    await pulsera.botonBorrar.click();
    await expect(regalitos.tarjetas).toHaveCount(1);
    await pulsera.botonBorrar.click();
    await expect(regalitos.tarjetas).toHaveCount(0);
  });

  test('REG-BOR-03 tras redibujar la pantalla vuelve a pedir confirmación @P0', async ({ regalitos }) => {
    const pulsera = await regalitos.agregarPulsera({ nombre: 'P1', receta: [] });
    await pulsera.botonBorrar.click();
    await pulsera.tipoDeGanancia.selectOption('fija');
    await expect(pulsera.botonBorrar).toHaveText('Borrar pulsera');
    await pulsera.botonBorrar.click();
    await expect(regalitos.tarjetas).toHaveCount(1);
  });
});
