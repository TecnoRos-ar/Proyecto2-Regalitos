/* Datos de prueba del plan (21.81-plan-de-pruebas.md, sección 5). */
import type { Material, Pulsera } from '../pages/regalitos.page';

export const hilo: Material = { nombre: 'Hilo encerado', unidad: 'm', stock: 50, pague: 3500, por: 50 };
export const mostacillas: Material = { nombre: 'Mostacillas', unidad: 'un', stock: 200, pague: 2000, por: 500 };
export const cierre: Material = { nombre: 'Cierre', unidad: 'un', stock: 10, pague: 1500, por: 10 };

/* DS-01: taller base. Costo esperado de "Clásica": $ 400,00. */
export const materiales: Material[] = [hilo, mostacillas, cierre];
export const clasica: Pulsera = {
  nombre: 'Clásica',
  receta: [
    { material: 'Hilo encerado', cantidad: 0.5 },
    { material: 'Mostacillas', cantidad: 20 },
    { material: 'Cierre', cantidad: 1 },
  ],
  manoObra: 100,
  otros: 35,
};

/* DS-02: variantes de precio sobre "Clásica". */
export const variantesDePrecio = [
  { tipo: 'pct', valor: 100, redondeo: 0, precio: '$ 800,00', ganancia: 'Ganancia $ 400,00 · 50% del precio' },
  { tipo: 'fija', valor: 250, redondeo: 0, precio: '$ 650,00', ganancia: 'Ganancia $ 250,00 · 38,46% del precio' },
  { tipo: 'fija', valor: 250, redondeo: 50, precio: '$ 650,00', ganancia: 'Ganancia $ 250,00 · 38,46% del precio' },
  { tipo: 'fija', valor: 250, redondeo: 100, precio: '$ 700,00', ganancia: 'Ganancia $ 300,00 · 42,86% del precio' },
  { tipo: 'pct', valor: 100, redondeo: 500, precio: '$ 1.000', ganancia: 'Ganancia $ 600,00 · 60% del precio' },
  { tipo: 'pct', valor: 0, redondeo: 100, precio: '$ 400,00', ganancia: 'Ganancia $ 0,00 · 0% del precio' },
  { tipo: 'pct', valor: 33, redondeo: 10, precio: '$ 540,00', ganancia: 'Ganancia $ 140,00 · 25,93% del precio' },
] as const;
