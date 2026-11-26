// Prueba en el navegador: registro, aprobación, login con 2FA, guardado con
// reintento ante una caída de red (REG-HAL-09), rechazo por permisos y cambio de contraseña.
// Uso (backend sobre una base vacía, admin sin 2FA configurado todavía):
//   PLAYWRIGHT=/ruta/node_modules/playwright BASE=http://127.0.0.1:8080 \
//   ADMIN_EMAIL=... ADMIN_PASSWORD=... node tests/e2e_navegador.cjs
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright');
const crypto = require('crypto');

const BASE = process.env.BASE || 'http://127.0.0.1:8080';
const fallas = [];
const paso = (nombre, ok, det = '') => {
  console.log((ok ? 'PASA ' : 'FALLA') + ' | ' + nombre + (ok ? '' : ' | ' + det));
  if (!ok) fallas.push(nombre);
};

function totp(secret, offset = 0) {
  const alf = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
  let bits = '';
  for (const c of secret.replace(/=+$/, '')) bits += alf.indexOf(c).toString(2).padStart(5, '0');
  const key = Buffer.from(bits.match(/.{8}/g).map(b => parseInt(b, 2)));
  const buf = Buffer.alloc(8);
  buf.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000) + offset));
  const h = crypto.createHmac('sha1', key).update(buf).digest();
  const o = h[h.length - 1] & 15;
  return String((h.readUInt32BE(o) & 0x7fffffff) % 1000000).padStart(6, '0');
}

async function api(method, path, body, cookie) {
  const r = await fetch(BASE + path, {
    method, headers: { 'content-type': 'application/json', ...(cookie ? { cookie: 'session=' + cookie } : {}) },
    body: body ? JSON.stringify(body) : undefined
  });
  const sc = r.headers.get('set-cookie') || '';
  return { status: r.status, json: await r.json().catch(() => null), cookie: (sc.match(/^session=([^;]*)/) || [])[1] };
}

(async () => {
  // Admin por API: configura su 2FA y obtiene sesión.
  let r = await api('POST', '/api/auth/login', { email: process.env.ADMIN_EMAIL, password: process.env.ADMIN_PASSWORD });
  const tmp = r.json.tmp;
  r = await api('POST', '/api/auth/totp/setup', { tmp });
  r = await api('POST', '/api/auth/totp/enable', { tmp, code: totp(r.json.secret) });
  const ADM = r.cookie;
  paso('Admin entra por API', r.status === 200 && !!ADM, r.status);
  await api('PUT', '/api/state', { materiales: [{ id: 'base1', nombre: 'Hilo base', unidad: 'm' }], productos: [], config: {} }, ADM);

  const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
  const page = await browser.newPage();
  const email = 'nav' + crypto.randomBytes(3).toString('hex') + '@ejemplo.com';
  const clave = 'clave-' + crypto.randomBytes(4).toString('hex');

  // 1) Registro por la pantalla
  await page.goto(BASE + '/login');
  await page.click('a[href="/register"]');
  await page.fill('#name', 'Prueba Navegador');
  await page.fill('#email', email);
  await page.fill('#p1', clave);
  await page.fill('#p2', clave + 'x');
  await page.click('#b');
  paso('Registro avisa si las contraseñas no coinciden', (await page.textContent('#err')).includes('no coinciden'));
  await page.fill('#p2', clave);
  await page.click('#b');
  await page.waitForSelector('#done:not(.hidden)');
  paso('Registro muestra cuenta pendiente de aprobación', (await page.textContent('#done')).includes('administrador'));

  // 2) Pendiente: el login avisa
  await page.goto(BASE + '/login');
  await page.fill('#email', email);
  await page.fill('#password', clave);
  await page.click('#b1');
  await page.waitForFunction(() => document.getElementById('err').textContent.length > 0);
  paso('Login de cuenta pendiente muestra el aviso', (await page.textContent('#err')).includes('pendiente'));

  // 3) El admin aprueba en /admin
  await page.context().addCookies([{ name: 'session', value: ADM, url: BASE }]);
  await page.goto(BASE + '/admin');
  const fila = page.locator('tr', { hasText: email });
  await fila.waitFor();
  paso('El panel muestra la cuenta como pendiente', (await fila.textContent()).includes('pendiente'));
  page.once('dialog', d => d.accept());
  await fila.locator('button[data-action="toggle"]').click();
  await page.waitForFunction(e => [...document.querySelectorAll('tr')].some(t => t.textContent.includes(e) && t.textContent.includes('Desactivar')), email);
  paso('El admin aprueba la cuenta desde el panel', true);
  await page.waitForSelector('#history tr');
  paso('El panel muestra el historial de datos', (await page.locator('#history tr').count()) >= 1);
  await page.context().clearCookies();

  // 4) Login de la cuenta aprobada con alta de 2FA
  await page.goto(BASE + '/login');
  await page.fill('#email', email);
  await page.fill('#password', clave);
  const setupResp = page.waitForResponse(x => x.url().endsWith('/api/auth/totp/setup'));
  await page.click('#b1');
  const secret = (await (await setupResp).json()).secret;
  await page.fill('#code', totp(secret));
  await Promise.all([page.waitForURL(BASE + '/'), page.click('#b2')]);
  paso('La cuenta aprobada entra con 2FA', true);

  // 5) Guardado con caída de red: se reintenta y no se pierde el cambio (REG-HAL-09)
  await page.goto(BASE + '/#materia-prima');
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('Guardado'));
  let fallos = 2;
  await page.route('**/api/state', route => {
    if (route.request().method() === 'PUT' && fallos > 0) { fallos--; return route.abort('failed'); }
    return route.continue();
  });
  await page.click('[data-a="add-m"]');
  await page.waitForFunction(() => document.getElementById('status').textContent.includes('Reintentando'), null, { timeout: 10000 });
  paso('Con la red caída avisa que reintenta', true);
  await page.waitForFunction(() => document.getElementById('status').textContent.trim() === 'Guardado', null, { timeout: 20000 });
  const estado = (await api('GET', '/api/state', null, ADM)).json.data;
  paso('Tras reintentar, el material nuevo quedó guardado', estado.materiales.length === 2, JSON.stringify(estado.materiales));
  await page.unroute('**/api/state');

  // 6) Employee intenta borrar: el servidor lo rechaza y la pantalla vuelve a lo guardado
  const borrar = page.locator('[data-a="del-m"]').first();
  await borrar.click();
  await borrar.click(); // confirmación: segundo toque
  const rechazo = await page.waitForFunction(() => document.getElementById('status').textContent.includes('No se guardó'), null, { timeout: 8000 }).then(() => true, () => false);
  const despues = (await api('GET', '/api/state', null, ADM)).json.data;
  paso('Employee no puede borrar: aviso en pantalla y datos intactos', rechazo && despues.materiales.length === 2,
       (await page.textContent('#status')) + ' / ' + despues.materiales.length);

  // 7) Cambio de contraseña desde la pantalla
  await page.goto(BASE + '/');
  await page.click('a[href="/account"]');
  await page.fill('#cur', 'mala-clave-1');
  await page.fill('#p1', 'nueva-clave-123');
  await page.fill('#p2', 'nueva-clave-123');
  await page.click('#b');
  await page.waitForFunction(() => document.getElementById('err').textContent.length > 0);
  paso('Cambio con contraseña actual errónea muestra error', (await page.textContent('#err')).includes('actual'));
  await page.fill('#cur', clave);
  await page.click('#b');
  await page.waitForSelector('#done:not(.hidden)');
  paso('Cambio de contraseña exitoso', true);
  await page.goto(BASE + '/');
  await page.waitForFunction(() => !document.getElementById('auth-gate'));
  paso('Sigue con sesión después del cambio', page.url() === BASE + '/');
  r = await api('POST', '/api/auth/login', { email, password: 'nueva-clave-123' });
  paso('La contraseña nueva sirve para entrar', r.status === 200, r.status);

  await browser.close();
  console.log(fallas.length ? 'FALLAS: ' + fallas.length : '\nOK');
  process.exit(fallas.length ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });
