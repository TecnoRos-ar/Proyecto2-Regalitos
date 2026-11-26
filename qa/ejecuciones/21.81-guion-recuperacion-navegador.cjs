// 21.81 Regalitos: casos de pantalla de la recuperación de contraseña (/forgot y /reset).
// Se corre aislado con el lanzador de 21.83-casita:
//   BASE=http://localhost:5176 ADMIN_EMAIL=... GUION=<ruta a este archivo> SALIDA=<carpeta> ./21.83-correr-navegador.sh
// Requiere una base recién creada (administrador sin segundo factor) y el backend sin proveedor
// de correo. El enlace no llega por correo: quien lanza la corrida lee el token de la salida del
// servidor cuando aparece SALIDA/pedido-N y lo deja en SALIDA/token-N. El guion borra los dos
// archivos al leerlos. La clave nueva se genera en cada corrida y no se guarda.
const { chromium } = require(process.env.PLAYWRIGHT);
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const BASE = process.env.BASE || 'http://localhost:5176';
const SALIDA = process.env.SALIDA;
const EMAIL = process.env.ADMIN_EMAIL;
const cuenta = { PASA: 0, FALLA: 0, DATO: 0 };
const ver = (caso, paso, esperado, obtenido, ok) => {
  const estado = ok === null ? 'DATO' : ok ? 'PASA' : 'FALLA';
  cuenta[estado] += 1;
  console.log(`${caso} | ${paso} | ${esperado} | ${String(obtenido).replace(/\s+/g, ' ').slice(0, 300)} | ${estado}`);
};
const esperarToken = async (n) => {
  fs.writeFileSync(path.join(SALIDA, `pedido-${n}`), '');
  const archivo = path.join(SALIDA, `token-${n}`);
  for (let i = 0; i < 60; i += 1) {
    if (fs.existsSync(archivo)) {
      const token = fs.readFileSync(archivo, 'utf8').trim();
      fs.unlinkSync(archivo);
      fs.rmSync(path.join(SALIDA, `pedido-${n}`), { force: true });
      return token;
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  return '';
};

(async () => {
  const navegador = await chromium.launch({ channel: 'chrome', chromiumSandbox: false, headless: true });
  const contexto = await navegador.newContext({ locale: 'es-AR', viewport: { width: 1366, height: 768 } });
  const p = await contexto.newPage();
  p.setDefaultTimeout(8000);
  const problemas = [];
  p.on('pageerror', (x) => problemas.push(String(x).slice(0, 140)));
  const pedidos = [];
  p.on('request', (r) => { if (r.url().includes('/api/auth/password/')) pedidos.push(new URL(r.url()).pathname); });
  const foto = (nombre) => p.screenshot({ path: path.join(SALIDA, nombre + '.png'), fullPage: true });
  const ruta = () => new URL(p.url()).pathname + new URL(p.url()).search;
  const nueva = crypto.randomBytes(9).toString('base64url');

  await p.goto(BASE + '/login');
  await p.getByRole('link', { name: '¿Olvidaste tu contraseña?' }).click();
  await p.waitForURL('**/forgot');
  ver('BE-NAV-REC-01', 'En la página de ingreso, seguir "¿Olvidaste tu contraseña?"', 'lleva a /forgot', ruta(), ruta() === '/forgot');
  await foto('be-nav-rec-01-pedir');

  await p.fill('#email', 'nadie@regalitos.local');
  await p.click('#b');
  await p.waitForSelector('#done:not(.hidden)');
  const avisoDesconocido = (await p.textContent('#done')).replace(/\s+/g, ' ').trim();
  ver('BE-NAV-REC-02', 'Pedir el enlace con un correo sin cuenta', 'aviso general, sin decir si la cuenta existe', avisoDesconocido,
    /Si el email está registrado/.test(avisoDesconocido));

  await p.goto(BASE + '/forgot');
  await p.fill('#email', EMAIL);
  await p.click('#b');
  await p.waitForSelector('#done:not(.hidden)');
  const aviso = (await p.textContent('#done')).replace(/\s+/g, ' ').trim();
  const campoOculto = await p.locator('#ask').evaluate((e) => e.classList.contains('hidden'));
  ver('BE-NAV-REC-03', 'Pedir el enlace con el correo de una cuenta', 'el mismo aviso y el formulario se oculta',
    `mismo_aviso=${aviso === avisoDesconocido} formulario_oculto=${campoOculto}`, aviso === avisoDesconocido && campoOculto);
  await foto('be-nav-rec-03-aviso');
  const token = await esperarToken(1);
  ver('BE-NAV-REC-04', 'El servidor emite el enlace', 'token de 64 caracteres', `token de ${token.length} caracteres`, token.length === 64);

  await p.goto(BASE + '/reset');
  const sinToken = (await p.textContent('#err')).trim();
  const camposOcultos = await p.locator('#form_fields').evaluate((e) => e.classList.contains('hidden'));
  ver('BE-NAV-REC-05', 'Abrir /reset sin token', 'aviso de enlace inválido y sin campos', `${sinToken} campos_ocultos=${camposOcultos}`,
    /no es válido/.test(sinToken) && camposOcultos);
  await foto('be-nav-rec-05-sin-token');

  await p.goto(BASE + '/reset?token=' + token);
  await p.waitForSelector('#p1');
  ver('BE-NAV-REC-06', 'Abrir el enlace', 'muestra los campos y el token sale de la barra de direcciones', ruta(), ruta() === '/reset');
  await foto('be-nav-rec-06-nueva-clave');

  await p.fill('#p1', nueva);
  await p.fill('#p2', nueva + 'x');
  await p.click('#b');
  const distintas = (await p.textContent('#err')).trim();
  ver('BE-NAV-REC-07', 'Escribir dos contraseñas distintas', '"Las contraseñas no coinciden." y no se envía',
    `${distintas} pedidos=${pedidos.filter((x) => x.endsWith('/reset')).length}`,
    /no coinciden/.test(distintas) && !pedidos.some((x) => x.endsWith('/reset')));

  await p.fill('#p1', '1234567');
  await p.fill('#p2', '1234567');
  await p.click('#b');
  const corta = await p.locator('#p1').evaluate((e) => !e.checkValidity());
  ver('BE-NAV-REC-08', 'Escribir una contraseña de 7 caracteres', 'el navegador no deja enviar',
    `campo_invalido=${corta} pedidos=${pedidos.filter((x) => x.endsWith('/reset')).length} ruta=${ruta()}`,
    corta && !pedidos.some((x) => x.endsWith('/reset')) && ruta() === '/reset');

  await p.fill('#p1', ' '.repeat(8));
  await p.fill('#p2', ' '.repeat(8));
  await p.click('#b');
  await p.waitForFunction(() => document.getElementById('err').textContent !== '');
  const blanco = (await p.textContent('#err')).trim();
  ver('BE-NAV-REC-09', 'Escribir ocho espacios', 'rechazo con mensaje y sigue en /reset', `${blanco} ruta=${ruta()}`, blanco !== '' && ruta() === '/reset');
  await foto('be-nav-rec-09-en-blanco');

  await p.fill('#p1', nueva);
  await p.fill('#p2', nueva);
  await p.click('#b');
  await p.waitForSelector('#reset_ok:not(.hidden)');
  const listo = (await p.textContent('#reset_ok')).trim();
  ver('BE-NAV-REC-10', 'Elegir una contraseña válida', 'vuelve a /login con el aviso de cambio hecho', `${ruta()} ${listo}`,
    ruta() === '/login' && /se cambió/.test(listo));
  await foto('be-nav-rec-10-cambiada');

  await p.fill('#email', EMAIL);
  await p.fill('#password', nueva);
  await p.click('#b1');
  await p.waitForSelector('#setup_block:not(.hidden)');
  ver('BE-NAV-REC-11', 'Ingresar con la contraseña nueva', 'la acepta y sigue con el segundo factor', 'pide el alta del segundo factor', true);

  await p.goto(BASE + '/reset?token=' + token);
  await p.fill('#p1', nueva + '2');
  await p.fill('#p2', nueva + '2');
  await p.click('#b');
  await p.waitForFunction(() => document.getElementById('err').textContent !== '');
  const usado = (await p.textContent('#err')).trim();
  ver('BE-NAV-REC-12', 'Abrir el mismo enlace por segunda vez y enviar', 'aviso de enlace inválido o vencido', usado, /no es válido o ya venció/.test(usado));
  await foto('be-nav-rec-12-enlace-usado');

  ver('BE-NAV-REC-13', 'Errores de JavaScript durante la corrida', 'ninguno', problemas.join(' / ') || 'ninguno', problemas.length === 0);
  console.log(`TOTAL | PASA ${cuenta.PASA} | FALLA ${cuenta.FALLA} | DATO ${cuenta.DATO}`);
  await navegador.close();
})().catch((e) => { console.log('ERROR | ' + String(e).replace(/[0-9a-f]{64}/g, '[token]').slice(0, 300)); process.exit(1); });
