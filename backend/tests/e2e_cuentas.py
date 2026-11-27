#!/usr/bin/env python3
"""Pruebas de punta a punta: registro, login con 2FA, cambio de contraseña,
permisos por rol sobre /api/state, historial y cookie de sesión.

Uso (backend recién levantado sobre una base vacía, con COOKIE_SECURE=1):
    BASE=http://127.0.0.1:8080 ADMIN_EMAIL=... ADMIN_PASSWORD=... python3 -I tests/e2e_cuentas.py
Sale con código 1 si falla algún paso.
"""
import base64, hashlib, hmac, json, os, secrets, struct, sys, time
import urllib.request, urllib.error

BASE = os.environ.get("BASE", "http://127.0.0.1:8080")
ADMIN_EMAIL = os.environ["ADMIN_EMAIL"]
ADMIN_PASSWORD = os.environ["ADMIN_PASSWORD"]
FALLAS = []


def paso(nombre, ok, detalle=""):
    print(("PASA " if ok else "FALLA") + f" | {nombre}" + (f" | {detalle}" if not ok else ""), flush=True)
    if not ok:
        FALLAS.append(nombre)


def llamar(metodo, ruta, cuerpo=None, cookie=None, headers=None):
    datos = json.dumps(cuerpo).encode() if cuerpo is not None else None
    req = urllib.request.Request(BASE + ruta, data=datos, method=metodo)
    if datos is not None:
        req.add_header("Content-Type", "application/json")
    if cookie:
        req.add_header("Cookie", "session=" + cookie)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        r = urllib.request.urlopen(req, timeout=30)
    except urllib.error.HTTPError as e:
        r = e
    texto = r.read().decode("utf-8", "replace")
    try:
        cuerpo = json.loads(texto) if texto else None
    except ValueError:
        cuerpo = texto
    return r.status, cuerpo, r.headers.get("Set-Cookie", "")


def cookie_de(set_cookie):
    return set_cookie.split(";")[0].split("=", 1)[1] if set_cookie.startswith("session=") else ""


def totp(secreto, desfase=0):
    clave = base64.b32decode(secreto + "=" * (-len(secreto) % 8))
    paso_t = int(time.time()) // 30 + desfase
    h = hmac.new(clave, struct.pack(">Q", paso_t), hashlib.sha1).digest()
    o = h[-1] & 15
    return "%06d" % ((struct.unpack(">I", h[o:o + 4])[0] & 0x7FFFFFFF) % 1000000)


def entrar_nuevo(email, clave):
    """Login de una cuenta sin 2FA: configura el 2FA y devuelve (cookie, secreto, código usado)."""
    _, c, _ = llamar("POST", "/api/auth/login", {"email": email, "password": clave})
    tmp = c["tmp"]
    _, c, _ = llamar("POST", "/api/auth/totp/setup", {"tmp": tmp})
    sec = c["secret"]
    cod = totp(sec)
    st, _, sc = llamar("POST", "/api/auth/totp/enable", {"tmp": tmp, "code": cod})
    assert st == 200, st
    return cookie_de(sc), sec, cod, sc


# ---------- admin ----------
ADM, ADM_SEC, _, sc = entrar_nuevo(ADMIN_EMAIL, ADMIN_PASSWORD)
paso("Cookie de sesión con HttpOnly, SameSite y Secure (REG-HAL-41)",
     all(a in sc for a in ("HttpOnly", "SameSite=Lax", "Secure")), sc)

# ---------- 2FA de un solo uso (REG-HAL-32) ----------
_, c, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL, "password": ADMIN_PASSWORD})
cod = totp(ADM_SEC, +1)
st1, _, _ = llamar("POST", "/api/auth/totp/verify", {"tmp": c["tmp"], "code": cod})
_, c2, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL, "password": ADMIN_PASSWORD})
st2, b2, _ = llamar("POST", "/api/auth/totp/verify", {"tmp": c2["tmp"], "code": cod})
paso("Código 2FA aceptado la primera vez", st1 == 200, st1)
paso("El mismo código 2FA se rechaza la segunda vez", st2 == 401, f"{st2} {b2}")
st3, _, _ = llamar("POST", "/api/auth/totp/verify", {"tmp": c2["tmp"], "code": totp(ADM_SEC, 0)})
paso("Un código anterior al último usado también se rechaza", st3 == 401, st3)

# ---------- registro ----------
suf = secrets.token_hex(3)
EMAIL = f"Nueva.{suf}@Ejemplo.com"
CLAVE = "clave-" + secrets.token_hex(4)
for nombre, cuerpo, esperado in [
    ("Registro con correo inválido", {"email": "sin-arroba", "password": CLAVE}, 400),
    ("Registro con contraseña corta", {"email": "a@b.com", "password": "corta"}, 400),
    ("Registro con contraseña de espacios", {"email": "a@b.com", "password": " " * 10}, 400),
    ("Registro con correo numérico", {"email": 5, "password": CLAVE}, 400),
]:
    st, b, _ = llamar("POST", "/api/auth/register", cuerpo)
    paso(nombre + f" → {esperado}", st == esperado, f"{st} {b}")
st, b, _ = llamar("POST", "/api/auth/register", {"email": EMAIL, "password": CLAVE, "name": "Nueva"})
paso("Registro válido → 202", st == 202, f"{st} {b}")
st2, b2, _ = llamar("POST", "/api/auth/register", {"email": EMAIL.lower(), "password": "otra-clave-123"})
paso("Registro repetido responde igual (no revela cuentas)", st2 == 202 and b2 == b, f"{st2} {b2}")
st, b, _ = llamar("POST", "/api/auth/login", {"email": EMAIL, "password": CLAVE})
paso("Cuenta pendiente no puede entrar (403 con aviso)", st == 403 and "pendiente" in json.dumps(b, ensure_ascii=False), f"{st} {b}")

_, usuarios, _ = llamar("GET", "/api/admin/users", cookie=ADM)
nueva = [u for u in usuarios if u["email"] == EMAIL.lower()]
paso("El admin ve una sola cuenta nueva, pendiente y employee",
     len(nueva) == 1 and nueva[0]["pending"] and not nueva[0]["active"] and nueva[0]["role"] == "employee", nueva)
st, _, _ = llamar("PUT", f"/api/admin/users/{nueva[0]['id']}", {"active": True}, cookie=ADM)
paso("El admin aprueba la cuenta", st == 200, st)
EMP, EMP_SEC, _, _ = entrar_nuevo(EMAIL.upper(), CLAVE)
paso("La cuenta aprobada entra (correo sin distinguir mayúsculas)", bool(EMP))
paso("El registro repetido no cambió la contraseña",
     llamar("POST", "/api/auth/login", {"email": EMAIL, "password": "otra-clave-123"})[0] == 401)
st, me, _ = llamar("GET", "/api/auth/me", cookie=EMP)
paso("/api/auth/me de la cuenta nueva", st == 200 and me["role"] == "employee", f"{st} {me}")

# ---------- cambio de contraseña ----------
_, c, _ = llamar("POST", "/api/auth/login", {"email": EMAIL, "password": CLAVE})
_, _, sc = llamar("POST", "/api/auth/totp/verify", {"tmp": c["tmp"], "code": totp(EMP_SEC, +1)})
EMP_OTRA = cookie_de(sc)  # otra sesión abierta de la misma cuenta
NUEVA = "nueva-" + secrets.token_hex(4)
casos = [
    ("Cambio sin sesión → 401", None, {"current_password": CLAVE, "new_password": NUEVA}, 401),
    ("Cambio con contraseña actual errónea → 401", EMP, {"current_password": "mala-mala", "new_password": NUEVA}, 401),
    ("Cambio con contraseña nueva corta → 400", EMP, {"current_password": CLAVE, "new_password": "corta"}, 400),
    ("Cambio con contraseña nueva igual → 400", EMP, {"current_password": CLAVE, "new_password": CLAVE}, 400),
    ("Cambio con tipo inválido → 400", EMP, {"current_password": CLAVE, "new_password": 12345678}, 400),
]
for nombre, ck, cuerpo, esp in casos:
    st, b, _ = llamar("POST", "/api/auth/password/change", cuerpo, cookie=ck)
    paso(nombre, st == esp, f"{st} {b}")
st, b, sc = llamar("POST", "/api/auth/password/change", {"current_password": CLAVE, "new_password": NUEVA}, cookie=EMP)
EMP2 = cookie_de(sc)
paso("Cambio de contraseña válido → 200 con cookie nueva", st == 200 and EMP2, f"{st} {b}")
paso("La sesión nueva sigue funcionando", llamar("GET", "/api/auth/me", cookie=EMP2)[0] == 200)
paso("La sesión vieja de este navegador queda cerrada", llamar("GET", "/api/auth/me", cookie=EMP)[0] == 401)
paso("La sesión en otro dispositivo queda cerrada", llamar("GET", "/api/auth/me", cookie=EMP_OTRA)[0] == 401)
paso("Login con la contraseña vieja → 401", llamar("POST", "/api/auth/login", {"email": EMAIL, "password": CLAVE})[0] == 401)
paso("Login con la contraseña nueva → 200", llamar("POST", "/api/auth/login", {"email": EMAIL, "password": NUEVA})[0] == 200)
EMP = EMP2

# ---------- permisos por rol sobre el estado (REG-HAL-43) ----------
base_estado = {"materiales": [{"id": "m1", "nombre": "Hilo"}, {"id": "m2", "nombre": "Dije"}],
               "productos": [{"id": "p1", "nombre": "Pulsera", "receta": []}], "config": {}, "ejemplo": False}
paso("Admin guarda el estado", llamar("PUT", "/api/state", base_estado, cookie=ADM)[0] == 200)
agregado = json.loads(json.dumps(base_estado)); agregado["materiales"].append({"id": "m3", "nombre": "Cuenta"})
paso("Employee puede agregar un material", llamar("PUT", "/api/state", agregado, cookie=EMP)[0] == 200)
vacio = {"materiales": [], "productos": [], "config": {}}
st, b, _ = llamar("PUT", "/api/state", vacio, cookie=EMP)
paso("Employee NO puede vaciar el estado → 403", st == 403, f"{st} {b}")
sin_prod = json.loads(json.dumps(agregado)); sin_prod["productos"] = []
paso("Employee NO puede borrar un producto → 403", llamar("PUT", "/api/state", sin_prod, cookie=EMP)[0] == 403)
_, actual, _ = llamar("GET", "/api/state", cookie=EMP)
paso("El estado quedó intacto tras los rechazos", len(actual["data"]["materiales"]) == 3 and len(actual["data"]["productos"]) == 1, actual)
paso("Admin sí puede borrar", llamar("PUT", "/api/state", vacio, cookie=ADM)[0] == 200)

# ---------- historial y restauración ----------
st, hist, _ = llamar("GET", "/api/state/history", cookie=EMP)
paso("Employee no ve el historial → 403", st == 403, st)
st, hist, _ = llamar("GET", "/api/state/history", cookie=ADM)
paso("Admin ve el historial", st == 200 and len(hist) >= 2, f"{st} {hist}")
v3 = next((h for h in hist if h["materiales"] == 3), None)
st, _, _ = llamar("POST", f"/api/state/history/{v3['id']}/restore", cookie=ADM)
_, actual, _ = llamar("GET", "/api/state", cookie=ADM)
paso("Restaurar devuelve los 3 materiales", st == 200 and len(actual["data"]["materiales"]) == 3, actual)
paso("Restaurar una versión inexistente → 404", llamar("POST", "/api/state/history/99999999/restore", cookie=ADM)[0] == 404)

# ---------- materiales por API: borrar solo admin ----------
st, m, _ = llamar("POST", "/api/materials", {"name": "Hilo API", "unit": "m"}, cookie=EMP)
paso("Employee crea material por API", st == 201, f"{st} {m}")
paso("Employee NO borra material por API → 403", llamar("DELETE", f"/api/materials/{m['id']}", cookie=EMP)[0] == 403)
paso("Admin borra material por API → 204", llamar("DELETE", f"/api/materials/{m['id']}", cookie=ADM)[0] == 204)

# ---------- páginas ----------
for ruta, texto in [("/register", "Crear cuenta"), ("/account", "Cambiar contraseña"), ("/login", 'href="/register"')]:
    html = urllib.request.urlopen(BASE + ruta, timeout=30).read().decode()
    paso(f"Página {ruta}", texto in html)

# ---------- logout ----------
st, _, sc = llamar("POST", "/api/auth/logout", cookie=EMP)
paso("Logout borra la cookie con Secure", st == 200 and "Max-Age=0" in sc and "Secure" in sc, sc)
paso("Tras logout la sesión no vale", llamar("GET", "/api/auth/me", cookie=EMP)[0] == 401)

print(f"\n{'OK' if not FALLAS else 'FALLAS: ' + str(len(FALLAS))}")
sys.exit(1 if FALLAS else 0)
