import { expect, type Locator, type Page } from '@playwright/test';

export type Material = { nombre: string; unidad: 'm' | 'un' | 'g'; stock: number; pague: number; por: number };
export type Pulsera = {
  nombre: string;
  receta: { material: string; cantidad: number }[];
  manoObra?: number;
  otros?: number;
};

/* Tarjeta de un material en la pestaña "Materia prima". */
export class TarjetaMaterial {
  constructor(readonly raiz: Locator) {}
  get nombre() { return this.raiz.getByLabel('Nombre'); }
  get unidad() { return this.raiz.getByLabel('Se mide en'); }
  get stock() { return this.raiz.getByLabel(/^Stock actual/); }
  get pague() { return this.raiz.getByLabel(/^Pagué/); }
  get por() { return this.raiz.getByLabel(/^por \(/); }
  get costoUnitario() { return this.raiz.locator('[data-out^="mcost:"]'); }
  get valorDelStock() { return this.raiz.locator('[data-out^="mval:"]'); }
  get botonBorrar() { return this.raiz.locator('[data-a="del-m"]'); }
}

/* Tarjeta de un modelo en la pestaña "Pulseras". */
export class TarjetaPulsera {
  constructor(readonly raiz: Locator) {}
  get nombre() { return this.raiz.getByLabel('Nombre de la pulsera'); }
  get filas() { return this.raiz.locator('.ing'); }
  get manoObra() { return this.raiz.getByLabel('Mano de obra ($)'); }
  get otros() { return this.raiz.getByLabel('Packaging y otros ($)'); }
  get tipoDeGanancia() { return this.raiz.locator('[data-f="ganTipo"]'); }
  get valorDeGanancia() { return this.raiz.locator('[data-f="ganValor"]'); }
  get materiales() { return this.raiz.locator('[data-out^="pmat:"]'); }
  get extras() { return this.raiz.locator('[data-out^="pextra:"]'); }
  get costo() { return this.raiz.locator('[data-out^="pcost:"]'); }
  get precio() { return this.raiz.locator('[data-out^="pprice:"]'); }
  get ganancia() { return this.raiz.locator('[data-out^="pgain:"]'); }
  get capacidad() { return this.raiz.locator('[data-out^="pcap:"]'); }
  get mensaje() { return this.raiz.locator('[data-out^="pmsg:"]'); }
  get botonBorrar() { return this.raiz.locator('[data-a="del-p"]'); }

  async agregarFila(material: string, cantidad: number | string) {
    const n = await this.filas.count();
    await this.raiz.getByRole('button', { name: '+ Agregar material' }).click();
    const fila = this.filas.nth(n);
    const lista = fila.getByLabel('Material', { exact: true });
    const valor = await lista.locator('option', { hasText: material }).first().getAttribute('value');
    await lista.selectOption(valor!);
    await this.filas.nth(n).getByLabel('Cantidad').fill(String(cantidad));
  }

  async armar(cantidad: number | string) {
    await this.raiz.getByLabel('Armé (pulseras)').fill(String(cantidad));
    await this.raiz.getByRole('button', { name: 'Descontar del stock' }).click();
  }
}

export class RegalitosPage {
  readonly estado: Locator;
  readonly tarjetas: Locator;
  readonly valorTotalDelStock: Locator;
  readonly redondeo: Locator;

  constructor(readonly page: Page, private readonly url: string) {
    this.estado = page.locator('#status');
    this.tarjetas = page.locator('article.card');
    this.valorTotalDelStock = page.locator('[data-out="msum"]');
    this.redondeo = page.getByLabel('Redondear el precio hacia arriba a');
  }

  async abrir() {
    await this.page.goto(this.url);
    await expect(this.estado).not.toHaveText('Cargando…');
  }

  async irAMateriaPrima() {
    await this.page.getByRole('link', { name: 'Materia prima' }).click();
    await expect(this.page.getByRole('heading', { name: 'Materia prima' })).toBeVisible();
  }

  async irAPulseras() {
    await this.page.getByRole('link', { name: 'Pulseras' }).click();
    await expect(this.page.getByRole('heading', { name: 'Pulseras', exact: true })).toBeVisible();
  }

  material(indice: number) { return new TarjetaMaterial(this.tarjetas.nth(indice)); }
  pulsera(indice = 0) { return new TarjetaPulsera(this.tarjetas.nth(indice)); }

  async agregarMaterial(m: Material) {
    await this.irAMateriaPrima();
    const n = await this.tarjetas.count();
    await this.page.getByRole('button', { name: '+ Nuevo material' }).click();
    const t = this.material(n);
    await t.nombre.fill(m.nombre);
    await t.unidad.selectOption(m.unidad);
    await t.stock.fill(String(m.stock));
    await t.pague.fill(String(m.pague));
    await t.por.fill(String(m.por));
    return t;
  }

  async agregarPulsera(p: Pulsera) {
    await this.irAPulseras();
    const n = await this.tarjetas.count();
    await this.page.getByRole('button', { name: '+ Nueva pulsera' }).click();
    const t = this.pulsera(n);
    await t.nombre.fill(p.nombre);
    for (const fila of p.receta) await t.agregarFila(fila.material, fila.cantidad);
    await t.manoObra.fill(String(p.manoObra ?? 0));
    await t.otros.fill(String(p.otros ?? 0));
    return t;
  }

  /* Stock de cada material, en el orden de carga. Deja la pestaña "Materia prima". */
  async stocks() {
    await this.irAMateriaPrima();
    return this.page.getByLabel(/^Stock actual/).evaluateAll((campos) => campos.map((c) => (c as HTMLInputElement).value));
  }

  datosGuardados() {
    return this.page.evaluate(() => JSON.parse(localStorage.getItem('regalitos-argentinos-v1') ?? 'null'));
  }
}
