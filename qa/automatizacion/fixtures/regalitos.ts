import { existsSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { test as base, expect } from '@playwright/test';
import { RegalitosPage } from '../pages/regalitos.page';
import { clasica, materiales } from '../data/ds01';

const ARCHIVO = 'Regalitos Argentinos.html';
/* Dominios que la página puede pedir. Cualquier otro se corta y hace fallar la prueba. */
const DOMINIOS_PERMITIDOS = ['fonts.googleapis.com', 'fonts.gstatic.com'];

/* REGALITOS_HTML manda. Si no, se busca el archivo en el repositorio (qa/automatizacion)
   y en el clon del espacio de QA. */
function archivoBajoPrueba(): string {
  const candidatos = [
    process.env.REGALITOS_HTML,
    path.resolve(__dirname, '../../..', ARCHIVO),
    path.resolve(__dirname, '../../../../../30-39-codigo/31-repositorios/proyecto2-regalitos', ARCHIVO),
  ].filter((c): c is string => Boolean(c));
  const encontrado = candidatos.find((c) => existsSync(c));
  if (!encontrado) throw new Error(`No se encuentra "${ARCHIVO}". Indicar la ruta en REGALITOS_HTML.`);
  return encontrado;
}

type Fixtures = {
  /* true: la prueba corre sin conexión. */
  sinRed: boolean;
  /* Dominios pedidos por la página durante la prueba. */
  dominiosPedidos: Set<string>;
  /* La página abierta, sin datos. */
  regalitos: RegalitosPage;
  /* La página con los datos DS-01 ya cargados y la pestaña "Pulseras" abierta. */
  taller: RegalitosPage;
};

export const test = base.extend<Fixtures>({
  sinRed: [false, { option: true }],

  dominiosPedidos: async ({ context, sinRed }, use) => {
    const pedidos = new Set<string>();
    const cortados: string[] = [];
    await context.setOffline(sinRed);
    await context.route(/^https?:/, async (ruta) => {
      const host = new URL(ruta.request().url()).host;
      pedidos.add(host);
      if (DOMINIOS_PERMITIDOS.includes(host)) return ruta.continue();
      cortados.push(host);
      return ruta.abort();
    });
    await use(pedidos);
    expect(cortados, 'la página pidió dominios no permitidos').toEqual([]);
  },

  regalitos: async ({ page, dominiosPedidos: _ }, use) => {
    const regalitos = new RegalitosPage(page, pathToFileURL(archivoBajoPrueba()).href);
    await regalitos.abrir();
    await use(regalitos);
  },

  taller: async ({ regalitos }, use) => {
    for (const m of materiales) await regalitos.agregarMaterial(m);
    await regalitos.agregarPulsera(clasica);
    await use(regalitos);
  },
});

export { expect };
