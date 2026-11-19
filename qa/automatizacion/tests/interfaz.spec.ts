import { expect, test } from '../fixtures/regalitos';

test.describe('Interfaz', () => {
  test('REG-NAV-02 no hay desplazamiento horizontal @P1 @movil', async ({ taller, page }) => {
    for (const ir of [() => taller.irAPulseras(), () => taller.irAMateriaPrima()]) {
      await ir();
      const sobra = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
      expect(sobra, 'píxeles que sobresalen del ancho de la pantalla').toBeLessThanOrEqual(0);
    }
  });
});
