import { expect, test } from '../fixtures/regalitos';

test.describe('Interfaz', () => {
  test('REG-NAV-02 no hay desplazamiento horizontal @P1 @movil', async ({ taller, page }) => {
    for (const ir of [() => taller.irAPulseras(), () => taller.irAMateriaPrima()]) {
      await ir();
      const sobra = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
      expect(sobra, 'píxeles que sobresalen del ancho de la pantalla').toBeLessThanOrEqual(0);
    }
  });

  test('REG-NAV-04 los controles tienen nombre y los mensajes se anuncian (REG-HAL-23) @P2', async ({ taller, page }) => {
    expect(await taller.controlesSinNombre(), 'controles sin nombre en "Pulseras"').toEqual([]);
    await expect(page.getByRole('navigation', { name: 'Secciones' })).toBeVisible();
    await expect(page.getByRole('link', { name: 'Pulseras' })).toHaveAttribute('aria-current', 'page');
    await expect(page.locator('html')).toHaveAttribute('lang', 'es-AR');

    const clasica = taller.pulsera();
    await clasica.armar(11);
    await expect(clasica.mensaje).toBeVisible();
    expect.soft(await taller.seAnuncia(clasica.mensaje), 'el mensaje de "Descontar del stock" se anuncia').toBe(true);
    expect.soft(await taller.seAnuncia(taller.estado), 'el estado de guardado se anuncia').toBe(true);

    await taller.irAMateriaPrima();
    expect(await taller.controlesSinNombre(), 'controles sin nombre en "Materia prima"').toEqual([]);
    await expect(taller.material(0).campo('Stock actual (metros)')).toBeVisible();
  });

  test.describe('zoom al 200 %', () => {
    /* 1366 × 768 al 200 % equivale a una ventana de 683 × 384. No es el zoom real del navegador. */
    test.use({ viewport: { width: 683, height: 384 } });

    test('REG-NAV-05 con zoom al 200 % no hay desplazamiento horizontal @P2', async ({ taller }) => {
      expect(await taller.desbordeHorizontal(), 'píxeles que sobresalen en "Pulseras"').toBeLessThanOrEqual(0);
      const clasica = taller.pulsera();
      await clasica.precio.scrollIntoViewIfNeeded();
      await expect(clasica.precio).toBeInViewport();
      await clasica.armar(3);
      await expect(clasica.mensaje).toHaveText('Listo. Descontamos del stock los materiales de 3 pulseras.');
      await taller.irAMateriaPrima();
      expect(await taller.desbordeHorizontal(), 'píxeles que sobresalen en "Materia prima"').toBeLessThanOrEqual(0);
    });
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
