import { expect, test } from '../fixtures/regalitos';

test.describe('Arranque', () => {
  test.describe('sin red', () => {
    test.use({ sinRed: true });

    test('REG-ARR-01 abre y funciona sin red @P0 @movil', async ({ regalitos, page }) => {
      await expect(regalitos.estado).toHaveText('Se guarda solo en este navegador');
      await expect(page.getByText('Todavía no armaste ninguna pulsera.')).toBeVisible();
    });
  });

  test('REG-ARR-02 con red carga las fuentes y no pide nada más @P1', async ({ regalitos, page, dominiosPedidos }) => {
    await expect(regalitos.estado).toHaveText('Se guarda solo en este navegador');
    const fuentes = await page.evaluate(async () => {
      await document.fonts.ready;
      return [...new Set([...document.fonts].filter((f) => f.status === 'loaded').map((f) => f.family))].sort();
    });
    expect(fuentes).toEqual(['Bricolage Grotesque', 'Inter']);
    expect([...dominiosPedidos].sort()).toEqual(['fonts.googleapis.com', 'fonts.gstatic.com']);
  });

  test('REG-ARR-04 el aviso "Datos de ejemplo" no aparece con el uso normal (REG-HAL-10) @P2', async ({ taller, page }) => {
    /* Comportamiento actual. Falta que el equipo confirme si deben existir datos de ejemplo. */
    await expect(taller.avisoDeEjemplo).toHaveCount(0);
    await taller.irAMateriaPrima();
    await expect(taller.avisoDeEjemplo).toHaveCount(0);

    /* Solo aparece si se fuerza la marca en los datos guardados. */
    await taller.esperarGuardado();
    await taller.marcarComoEjemplo();
    await expect(taller.avisoDeEjemplo).toContainText('Cambiá los números por los tuyos o empezá de cero.');
    await page.getByRole('button', { name: 'Usar como base' }).click();
    await expect(taller.avisoDeEjemplo).toHaveCount(0);
    expect(await taller.stocks()).toEqual(['50', '200', '10']);

    await taller.irAPulseras();
    await taller.redondeo.selectOption('500');
    await taller.esperarGuardado();
    await taller.marcarComoEjemplo();
    await page.getByRole('button', { name: 'Borrar ejemplos' }).click();
    await expect(taller.avisoDeEjemplo).toHaveCount(0);
    await expect(page.getByText('Todavía no armaste ninguna pulsera.')).toBeVisible();
    await expect(taller.redondeo).toHaveValue('500');
    expect(await taller.stocks()).toEqual([]);
  });

  test('REG-NAV-01 las pestañas siguen a la dirección @P1', async ({ regalitos, page }) => {
    await regalitos.irAMateriaPrima();
    await expect(page).toHaveURL(/#materia-prima$/);
    await page.goBack();
    await expect(page.getByRole('heading', { name: 'Pulseras', exact: true })).toBeVisible();
    await expect(page.getByRole('link', { name: 'Pulseras' })).toHaveAttribute('aria-current', 'page');
  });
});
