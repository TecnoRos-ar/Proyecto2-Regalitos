import { expect, test } from '../fixtures/regalitos';
import { hilo } from '../data/ds01';

test.describe('Guardado', () => {
  test('REG-PER-01 los datos vuelven al recargar @P0', async ({ taller, page }) => {
    await expect(taller.estado).toHaveText('Guardado en este navegador');
    await page.reload();
    await expect(taller.estado).toHaveText('Se guarda solo en este navegador');
    const clasica = taller.pulsera();
    await expect(clasica.nombre).toHaveValue('Clásica');
    await expect(clasica.costo).toHaveText('$ 400,00');
    await expect(clasica.precio).toHaveText('$ 800,00');
    expect(await taller.stocks()).toEqual(['50', '200', '10']);
  });

  test('REG-PER-02 avisa cuando no se puede guardar @P0', async ({ regalitos, page }) => {
    await page.evaluate(() => {
      Storage.prototype.setItem = () => { throw new DOMException('cuota', 'QuotaExceededError'); };
    });
    await regalitos.agregarMaterial(hilo);
    await expect(regalitos.estado).toHaveText('No se pudo guardar. Probá de nuevo.');
  });

  test('REG-PER-03 con datos guardados dañados abre vacía @P1', async ({ regalitos, page }) => {
    await page.evaluate(() => localStorage.setItem('regalitos-argentinos-v1', '{dañado'));
    await page.reload();
    await expect(regalitos.estado).toHaveText('Se guarda solo en este navegador');
    await expect(page.getByText('Todavía no armaste ninguna pulsera.')).toBeVisible();
  });
});
