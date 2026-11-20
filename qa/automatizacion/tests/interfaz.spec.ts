import { expect, test } from '../fixtures/regalitos';

test.describe('Interfaz', () => {
  test('REG-NAV-02 no hay desplazamiento horizontal @P1 @movil', async ({ taller, page }) => {
    for (const ir of [() => taller.irAPulseras(), () => taller.irAMateriaPrima()]) {
      await ir();
      const sobra = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
      expect(sobra, 'píxeles que sobresalen del ancho de la pantalla').toBeLessThanOrEqual(0);
    }
  });

  test('REG-NAV-03 con teclado el foco no se pierde (REG-HAL-20) @P1', async ({ regalitos, page }) => {
    await regalitos.irAMateriaPrima();
    await regalitos.botonNuevoMaterial.focus();
    await page.keyboard.press('Enter');
    const material = regalitos.material(0);
    await expect(material.nombre).toBeFocused();

    await page.keyboard.type('Hilo');
    await page.keyboard.press('Tab');
    await expect(material.unidad).toBeFocused();
    await page.keyboard.press('ArrowUp');
    await expect(material.unidad).toHaveValue('m');
    await expect.soft(material.unidad, 'foco tras cambiar "Se mide en"').toBeFocused();

    await material.botonBorrar.focus();
    await page.keyboard.press('Enter');
    await expect(material.botonBorrar).toHaveText('¿Seguro? Tocá otra vez');
    await page.keyboard.press('Enter');
    await expect(regalitos.tarjetas).toHaveCount(0);
    await expect.soft(page.locator('body'), 'foco tras borrar el material: no debe volver al inicio de la página').not.toBeFocused();
  });
});
