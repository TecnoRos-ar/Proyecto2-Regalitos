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
  /* Campo o rótulo por su texto exacto: para comprobar los textos que dependen de la unidad. */
  campo(etiqueta: string) { return this.raiz.getByLabel(etiqueta, { exact: true }); }
  rotulo(texto: string) { return this.raiz.getByText(texto, { exact: true }); }
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
  /* Textos de la lista de materiales de una fila de la receta. */
  opcionesDeMaterial(fila = 0) { return this.filas.nth(fila).getByLabel('Material', { exact: true }).locator('option'); }
  materialDeFila(fila = 0) { return this.filas.nth(fila).getByLabel('Material', { exact: true }); }
  unidadDeFila(fila = 0) { return this.filas.nth(fila).locator('.u'); }
  subtotalDeFila(fila = 0) { return this.filas.nth(fila).locator('.isub'); }
  quitarFila(fila = 0) { return this.filas.nth(fila).getByRole('button', { name: 'Quitar material' }); }
  rotulo(texto: string) { return this.raiz.getByText(texto, { exact: true }); }

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
  readonly gananciaSugerida: Locator;
  readonly botonNuevoMaterial: Locator;
  readonly avisoDeEjemplo: Locator;

  constructor(readonly page: Page, private readonly url: string) {
    this.estado = page.locator('#status');
    this.tarjetas = page.locator('article.card');
    this.valorTotalDelStock = page.locator('[data-out="msum"]');
    this.redondeo = page.getByLabel('Redondear el precio hacia arriba a');
    this.gananciaSugerida = page.getByLabel('Ganancia sugerida para pulseras nuevas (%)');
    this.botonNuevoMaterial = page.getByRole('button', { name: '+ Nuevo material' });
    this.avisoDeEjemplo = page.getByRole('status').filter({ hasText: 'Datos de ejemplo.' });
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

  /* Stock de cada material, en el orden de carga, como número: el campo puede mostrarlo
     con coma o con punto. Deja la pestaña "Materia prima". */
  async stocks() {
    await this.irAMateriaPrima();
    return this.page.getByLabel(/^Stock actual/).evaluateAll((campos) => campos.map((c) => Number((c as HTMLInputElement).value.replace(',', '.'))));
  }

  /* Píxeles que sobresalen del ancho de la pantalla. */
  desbordeHorizontal() {
    return this.page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
  }

  /* Texto en negrita dentro de las tarjetas: no debe haberlo si un nombre trae HTML. */
  get negritasEnTarjetas() { return this.tarjetas.locator('b'); }

  /* Campos, listas y botones que un lector de pantalla anunciaría sin nombre. */
  controlesSinNombre() {
    return this.page.evaluate(() => [...document.querySelectorAll<HTMLElement>('input, select, button')]
      .filter((e) => !((e as HTMLInputElement).labels?.length) && !e.getAttribute('aria-label') && !e.textContent?.trim())
      .map((e) => e.outerHTML.slice(0, 80)));
  }

  /* true si un lector de pantalla anuncia los cambios de ese elemento. */
  seAnuncia(elemento: Locator) {
    return elemento.evaluate((e) => ['status', 'alert', 'log'].includes(e.getAttribute('role') ?? '')
      || ['polite', 'assertive'].includes(e.getAttribute('aria-live') ?? ''));
  }

  /* Marca los datos guardados como "de ejemplo": el uso normal no lo hace nunca (REG-HAL-10). */
  async marcarComoEjemplo() {
    await this.page.evaluate(() => {
      const clave = 'regalitos-argentinos-v1';
      const datos = JSON.parse(localStorage.getItem(clave)!);
      datos.ejemplo = true;
      localStorage.setItem(clave, JSON.stringify(datos));
    });
    await this.page.reload();
    await expect(this.estado).not.toHaveText('Cargando…');
  }

  /* Espera a que el cambio quede escrito en el navegador. */
  async esperarGuardado() { await expect(this.estado).toHaveText('Guardado en este navegador'); }

  datosGuardados() {
    return this.page.evaluate(() => JSON.parse(localStorage.getItem('regalitos-argentinos-v1') ?? 'null'));
  }
}
