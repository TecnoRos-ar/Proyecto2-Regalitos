// 21.81 Regalitos: casos de pantalla del backend (ingreso, segundo factor, guardado en el servidor).
// Se corre aislado con el lanzador de 21.83-casita:
//   BASE=http://localhost:5176 ADMIN_EMAIL=... ADMIN_PASSWORD=... GUION=<ruta a este archivo> SALIDA=<carpeta> ./21.83-correr-navegador.sh
// Requiere al administrador sin segundo factor configurado. La clave llega por variable de entorno y no se guarda.
const { chromium } = require(process.env.PLAYWRIGHT);
const crypto = require('crypto');
const path = require('path');

const BASE = process.env.BASE || 'http://localhost:5176';
const SALIDA = process.env.SALIDA;
const cuenta = { PASA: 0, FALLA: 0, DATO: 0 };
const ver = (caso, paso, esperado, obtenido, ok) => {
  const estado = ok === null ? 'DATO' : ok ? 'PASA' : 'FALLA';
  cuenta[estado] += 1;
  console.log(`${caso} | ${paso} | ${esperado} | ${String(obtenido).replace(/\s+/g, ' ').slice(0, 300)} | ${estado}`);
};
const totp = (secreto) => {
  const alfabeto = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
  const bits = [...secreto.replace(/=+$/, '')].map((c) => alfabeto.indexOf(c).toString(2).padStart(5, '0')).join('');
  const clave = Buffer.from(bits.match(/.{8}/g).map((b) => parseInt(b, 2)));
  const contador = Buffer.alloc(8);
  contador.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000)));
  const mac = crypto.createHmac('sha1', clave).update(contador).digest();
  const pos = mac[mac.length - 1] & 15;
  return String((mac.readUInt32BE(pos) & 0x7fffffff) % 1000000).padStart(6, '0');
};

(async () => {
  const navegador = await chromium.launch({ channel: 'chrome', chromiumSandbox: false, headless: true });
  const contexto = await navegador.newContext({ locale: 'es-AR', viewport: { width: 1366, height: 768 } });
  const p = await contexto.newPage();
  p.setDefaultTimeout(8000);
  const problemas = [];
  p.on('pageerror', (x) => problemas.push(String(x).slice(0, 140)));
  const foto = (nombre) => p.screenshot({ path: path.join(SALIDA, nombre + '.png'), fullPage: true });
  const estado = async () => (await (await p.request.get(BASE + '/api/state')).json()).data;

  await p.goto(BASE + '/');
  await p.waitForURL('**/login');
  ver('BE-NAV-01', 'Abrir la aplicación sin sesión', 'lleva a /login', new URL(p.url()).pathname, p.url().endsWith('/login'));

  await p.fill('#email', process.env.ADMIN_EMAIL);
  await p.fill('#password', 'clave-equivocada');
  await p.click('#b1');
  await p.waitForFunction(() => document.getElementById('err').textContent !== '');
  const error = await p.textContent('#err');
  ver('BE-NAV-02', 'Ingresar con clave equivocada', 'mensaje de credenciales inválidas', error, /inv[aá]lid/i.test(error));

  await p.fill('#password', process.env.ADMIN_PASSWORD);
  await p.click('#b1');
  await p.waitForSelector('#setup_block:not(.hidden)');
  const secreto = (await p.textContent('#secret')).trim();
  const qr = await p.locator('#qr_container svg').count();
  ver('BE-NAV-03', 'Primer ingreso: alta del segundo factor', 'código QR y clave manual', `QR=${qr} clave de ${secreto.length} caracteres`, qr === 1 && secreto.length >= 16);
  await foto('be-nav-03-alta-2fa');

  await p.fill('#code', totp(secreto));
  await p.click('#b2');
  await p.waitForURL(BASE + '/');
  await p.waitForSelector('#auth-gate', { state: 'detached' });
  const cabecera = (await p.locator('header, .right').first().innerText()).replace(/\s+/g, ' ');
  ver('BE-NAV-04', 'Confirmar el código', 'entra a la aplicación, con el correo, el rol y "Salir"', cabecera, /admin/.test(cabecera) && /Salir/.test(cabecera));
  const usuarios = await p.locator('nav.tabs a[href="/admin"]').count();
  ver('BE-NAV-05', 'El administrador ve el acceso a "Usuarios"', '1 enlace', usuarios, usuarios === 1);

  // Estado guardado por otra cuenta con HTML en un id (lo deja el guion de API, BE-EST-07).
  await p.goto(BASE + '/#materia-prima');
  await p.reload();
  await p.waitForSelector('#auth-gate', { state: 'detached' });
  await p.waitForTimeout(800);
  const inyectado = await p.evaluate(() => window.__qa === 1);
  const guardado = JSON.stringify(await estado()).includes('onerror');
  ver('BE-NAV-06', 'Abrir un estado guardado cuyo id de material contiene HTML', 'no se ejecuta código',
    guardado ? `ejecutado=${inyectado}` : 'el estado de prueba no estaba cargado', guardado ? !inyectado : null);
  await foto('be-nav-06-estado-con-html');

  await p.request.put(BASE + '/api/state', { data: { materiales: [], productos: [] } });
  await p.reload();
  await p.waitForSelector('[data-a="add-m"]');
  await p.locator('[data-a="add-m"]').first().click();
  await p.locator('[data-f="nombre"]').first().fill('Hilo encerado');
  const stock = p.locator('[data-f="stock"]').first();
  await stock.click();
  await stock.pressSequentially('0,5');
  await stock.press('Tab');
  await p.waitForFunction(() => /Guardado/.test(document.getElementById('status').textContent), null, { timeout: 8000 }).catch(() => {});
  const visible = await stock.inputValue();
  const servidor = ((await estado()) || { materiales: [] }).materiales[0] || {};
  ver('BE-NAV-07', 'Material nuevo con stock "0,5"', 'el campo muestra 0,5 y el servidor guarda 0.5',
    `campo=${visible} servidor=${servidor.stock} nombre=${servidor.nombre}`, visible === '0,5' && servidor.stock === 0.5);
  const aviso = await p.textContent('#status');
  ver('BE-NAV-08', 'Indicador de guardado', '"Guardado"', aviso, /^Guardado$/.test(aviso.trim()));
  await foto('be-nav-07-material');

  await p.reload();
  await p.waitForSelector('[data-f="nombre"]');
  const nombre = await p.locator('[data-f="nombre"]').first().inputValue();
  ver('BE-NAV-09', 'Recargar la página', 'el material sigue', nombre, nombre === 'Hilo encerado');

  await p.getByRole('button', { name: 'Salir' }).click();
  await p.waitForURL('**/login');
  await p.goto(BASE + '/');
  await p.waitForURL('**/login');
  ver('BE-NAV-10', 'Salir y volver a abrir la aplicación', 'pide ingresar de nuevo', new URL(p.url()).pathname, p.url().endsWith('/login'));

  ver('BE-NAV-11', 'Errores de JavaScript durante la corrida', 'ninguno', problemas.join(' / ') || 'ninguno', problemas.length === 0);
  console.log(`TOTAL | PASA ${cuenta.PASA} | FALLA ${cuenta.FALLA} | DATO ${cuenta.DATO}`);
  await navegador.close();
})().catch((e) => { console.log('ERROR | ' + String(e).slice(0, 300)); process.exit(1); });
