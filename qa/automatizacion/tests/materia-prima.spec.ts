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

  test('REG-MAT-06 un material sin stock lo indica @P1', async ({ regalitos, page }) => {
    await regalitos.agregarMaterial({ ...hilo, stock: 0 });
    await expect(page.getByText('Sin stock')).toBeVisible();
  });
});
