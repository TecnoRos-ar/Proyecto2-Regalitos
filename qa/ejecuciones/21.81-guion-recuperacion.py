#!/usr/bin/env python3
"""21.81 Regalitos: guion de pruebas de la recuperacion de contrasena (API HTTP).

Prueba el codigo de backend/recuperar-contrasena.zip aplicado sobre develop.
Uso (dentro de la VM de pruebas, con el backend y la base levantados):
    BASE=http://127.0.0.1:8080 ADMIN_EMAIL=... ADMIN_PASSWORD=... python3 -I 21.81-guion-recuperacion.py

Escribe una linea por paso: ID | paso | esperado | obtenido | PASA/FALLA/DATO.
Necesita una base recien creada y el backend sin proveedor de correo: asi el enlace
sale por el registro del contenedor, de donde lo lee el guion. Las claves se generan
en cada corrida y no se guardan; el enlace no se imprime.
"""
import base64, hashlib, hmac, json, os, re, secrets, struct, subprocess, time
import urllib.request, urllib.error

BASE = os.environ.get("BASE", "http://127.0.0.1:8080")
ADMIN_EMAIL = os.environ["ADMIN_EMAIL"]
ADMIN_PASSWORD = os.environ["ADMIN_PASSWORD"]
DB = os.environ.get("DB_CONTAINER", "regalitos-db")
BE = os.environ.get("BE_CONTAINER", "regalitos-be")
CUENTA = {"PASA": 0, "FALLA": 0, "DATO": 0}
GENERICO = "Si el email está registrado"


def paso(ident, texto, esperado, obtenido, ok):
    estado = ok if isinstance(ok, str) else ("PASA" if ok else "FALLA")
    CUENTA[estado] += 1
    print(f"{ident} | {texto} | {esperado} | {obtenido} | {estado}", flush=True)


def llamar(metodo, ruta, cuerpo=None, cookie=None, crudo=None):
    datos = crudo if crudo is not None else (json.dumps(cuerpo).encode() if cuerpo is not None else None)
    pedido = urllib.request.Request(BASE + ruta, data=datos, method=metodo)
    if datos is not None:
        pedido.add_header("Content-Type", "application/json")
    if cookie:
        pedido.add_header("Cookie", "session=" + cookie)
    try:
        resp = urllib.request.urlopen(pedido, timeout=30)
    except urllib.error.HTTPError as error:
        resp = error
    except Exception as error:  # conexion cortada
        return 0, {"_error": type(error).__name__}, ""
    texto = resp.read().decode("utf-8", "replace")
    try:
        cuerpo_json = json.loads(texto) if texto else None
    except ValueError:
        cuerpo_json = {"_texto": texto[:200]}
    return resp.status, cuerpo_json, resp.headers.get("Set-Cookie") or ""


def totp(secreto):
    clave = base64.b32decode(secreto + "=" * (-len(secreto) % 8))
    mac = hmac.new(clave, struct.pack(">Q", int(time.time()) // 30), hashlib.sha1).digest()
    pos = mac[-1] & 0x0F
    return "%06d" % ((struct.unpack(">I", mac[pos:pos + 4])[0] & 0x7FFFFFFF) % 1000000)


def sql(consulta):
    salida = subprocess.run(["docker", "exec", DB, "psql", "-U", "regalitos", "-d", "regalitos", "-tAc", consulta],
                            capture_output=True, text=True)
    return salida.stdout.strip()


def cookie_de(cabecera):
    return cabecera.split("session=", 1)[1].split(";", 1)[0] if "session=" in cabecera else ""


def pedidos(email):
    return int(sql(f"SELECT count(*) FROM password_resets pr JOIN users u ON u.id = pr.user_id WHERE u.email = '{email}'") or 0)


def pedir(email):
    """Pide el enlace y devuelve (estado, cuerpo, token leido del registro o '')."""
    antes = pedidos(email)
    est, cuerpo, _ = llamar("POST", "/api/auth/password/forgot", {"email": email})
    time.sleep(1.5)  # el aviso sale en segundo plano
    if pedidos(email) == antes:
        return est, cuerpo, ""
    registro = subprocess.run(["docker", "logs", "--tail", "40", BE], capture_output=True, text=True)
    tokens = re.findall(r"reset\?token=([0-9a-f]{64})", registro.stdout + registro.stderr)
    return est, cuerpo, tokens[-1] if tokens else ""


def cambiar(token, clave):
    return llamar("POST", "/api/auth/password/reset", {"token": token, "password": clave})


def entra(email, clave):
    return llamar("POST", "/api/auth/login", {"email": email, "password": clave})


# ---------- Preparacion: administrador con segundo factor y dos cuentas ----------
_, cuerpo, _ = entra(ADMIN_EMAIL, ADMIN_PASSWORD)
tmp = (cuerpo or {}).get("tmp", "")
_, alta, _ = llamar("POST", "/api/auth/totp/setup", {"tmp": tmp})
SECRETO = (alta or {}).get("secret", "")
_, _, cabecera = llamar("POST", "/api/auth/totp/enable", {"tmp": tmp, "code": totp(SECRETO)})
ADMIN = cookie_de(cabecera)
CLAVE_EMP = secrets.token_urlsafe(12)
EMP1, EMP2 = "recupera1@regalitos.local", "recupera2@regalitos.local"
creadas = [llamar("POST", "/api/admin/users", {"email": e, "password": CLAVE_EMP, "name": "Prueba"}, ADMIN)[0] for e in (EMP1, EMP2)]
est, _, _ = llamar("GET", "/api/auth/me", cookie=ADMIN)
paso("BE-REC-00", "Preparacion: sesion del administrador y dos cuentas de prueba", "200 y 201, 201", f"{est} {creadas}",
     est == 200 and creadas == [201, 201])

# ---------- Paginas ----------
estados = [llamar("GET", r)[0] for r in ("/forgot", "/reset")]
paso("BE-REC-01", "GET /forgot y GET /reset sin sesion", "200 y 200", estados, estados == [200, 200])
pagina = urllib.request.urlopen(BASE + "/login", timeout=30).read().decode("utf-8", "replace")
paso("BE-REC-02", "La pagina de ingreso enlaza a la recuperacion", 'enlace a "/forgot"', 'href="/forgot"' in pagina, 'href="/forgot"' in pagina)

# ---------- Pedir el enlace ----------
est, cuerpo, _ = llamar("POST", "/api/auth/password/forgot", {"email": "nadie@regalitos.local"})
filas = sql("SELECT count(*) FROM password_resets")
paso("BE-REC-03", "Pedir el enlace para un correo sin cuenta", "200 con el mensaje general y ningun pedido guardado",
     f"{est} {str((cuerpo or {}).get('message'))[:40]} pedidos={filas}",
     est == 200 and GENERICO in str((cuerpo or {}).get("message")) and filas == "0")
desconocido = cuerpo
est1, _, _ = llamar("POST", "/api/auth/password/forgot", {})
est2, _, _ = llamar("POST", "/api/auth/password/forgot", crudo=b"{no es json")
paso("BE-REC-04", "Pedir el enlace sin correo y con un cuerpo mal formado", "400 y 400", f"{est1} {est2}", [est1, est2] == [400, 400])
est, cuerpo, _ = llamar("POST", "/api/auth/password/forgot", {"email": 12345})
paso("BE-REC-05", "Pedir el enlace con un numero como correo", "400", f"{est} {str(cuerpo)[:60]}", est == 400)

est, cuerpo, T1 = pedir(ADMIN_EMAIL)
paso("BE-REC-06", "Pedir el enlace para una cuenta existente", "200 con la misma respuesta que para un correo sin cuenta",
     f"{est} igual={cuerpo == desconocido}", est == 200 and cuerpo == desconocido)
guardado = sql(f"SELECT token_hash FROM password_resets pr JOIN users u ON u.id = pr.user_id WHERE u.email = '{ADMIN_EMAIL}' ORDER BY pr.created_at DESC LIMIT 1")
paso("BE-REC-07", "Sin proveedor de correo, el enlace sale por el registro del servidor", "enlace con token de 64 caracteres",
     f"token de {len(T1)} caracteres", len(T1) == 64)
paso("BE-REC-08", "La base guarda el resumen del token, no el token", "resumen SHA-256 distinto del token",
     f"coincide_con_sha256={guardado == hashlib.sha256(T1.encode()).hexdigest()} igual_al_token={guardado == T1}",
     guardado == hashlib.sha256(T1.encode()).hexdigest() and guardado != T1)
minutos = sql(f"SELECT round(extract(epoch FROM (expires_at - created_at)) / 60) FROM password_resets WHERE token_hash = '{guardado}'")
paso("BE-REC-09", "Vigencia del enlace", "30 minutos", minutos, minutos == "30")

# ---------- Cambiar la contrasena ----------
NUEVA = secrets.token_urlsafe(12)
est1, _, _ = cambiar("no-es-un-token", NUEVA)
est2, _, _ = cambiar("0" * 64, NUEVA)
est3, _, _ = llamar("POST", "/api/auth/password/reset", {"password": NUEVA})
paso("BE-REC-10", "Cambiar con un token mal formado, uno inexistente y sin token", "400, 400 y 400", f"{est1} {est2} {est3}", [est1, est2, est3] == [400, 400, 400])
est, cuerpo, _ = cambiar(T1, "1234567")
sigue, _, _ = entra(ADMIN_EMAIL, ADMIN_PASSWORD)
paso("BE-REC-11", "Cambiar con una contrasena de 7 caracteres", "400 y la contrasena anterior sigue valiendo",
     f"{est} ingreso_con_la_anterior={sigue}", est == 400 and sigue == 200)
est, cuerpo, _ = llamar("POST", "/api/auth/password/reset", {"token": T1, "password": 12345678})
paso("BE-REC-12", "Cambiar con un numero como contrasena", "400", f"{est} {str(cuerpo)[:60]}", est == 400)
est, cuerpo, _ = cambiar(T1, NUEVA)
paso("BE-REC-13", "Cambiar la contrasena con el enlace vigente", "200", f"{est} {cuerpo}", est == 200)
vieja, _, _ = entra(ADMIN_EMAIL, ADMIN_PASSWORD)
nueva, cuerpo, cabecera = entra(ADMIN_EMAIL, NUEVA)
paso("BE-REC-14", "Ingresar con la contrasena anterior y con la nueva", "401 y 200", f"{vieja} {nueva}", [vieja, nueva] == [401, 200])
paso("BE-REC-15", "Tras el cambio, el ingreso sigue pidiendo el segundo factor", "pide el codigo; no entrega sesion",
     f"claves={sorted((cuerpo or {}).keys())} sesion={bool(cookie_de(cabecera))}", not cookie_de(cabecera) and "tmp" in (cuerpo or {}))
est, _, _ = llamar("GET", "/api/auth/me", cookie=ADMIN)
paso("BE-REC-16", "Una sesion abierta antes del cambio de contrasena", "401", est, est == 401)
est, _, _ = cambiar(T1, secrets.token_urlsafe(12))
sigue, _, _ = entra(ADMIN_EMAIL, NUEVA)
paso("BE-REC-17", "Usar el mismo enlace por segunda vez", "400 y la contrasena no cambia", f"{est} ingreso={sigue}", est == 400 and sigue == 200)

# ---------- Varios pedidos ----------
_, _, T2 = pedir(ADMIN_EMAIL)
_, _, T3 = pedir(ADMIN_EMAIL)
est, _, _ = cambiar(T2, secrets.token_urlsafe(12))
paso("BE-REC-18", "Un enlace nuevo anula el anterior sin usar", "400 con el enlace anterior", f"{est} enlaces_distintos={T2 != T3 and len(T3) == 64}",
     est == 400 and T2 != T3 and len(T3) == 64)
est, cuerpo, T4 = pedir(ADMIN_EMAIL)
paso("BE-REC-19", "Cuarto pedido de la misma cuenta en una hora", "200 con el mensaje general y sin enlace nuevo (maximo 3 por hora)",
     f"{est} pedidos={pedidos(ADMIN_EMAIL)} enlace_nuevo={bool(T4)}", est == 200 and pedidos(ADMIN_EMAIL) == 3 and not T4)
paso("BE-REC-20", "Quien conoce el correo de una cuenta agota sus tres pedidos de la hora", "la cuenta sigue pudiendo pedir su enlace",
     f"pedidos sin sesion desde cualquier origen; al cuarto la cuenta no recibe enlace (enlace_nuevo={bool(T4)})", "DATO")
est, _, _ = cambiar(T3, NUEVA)
paso("BE-REC-21", "El ultimo enlace vigente sigue sirviendo", "200", est, est == 200)

# ---------- Vencimiento, cuenta inactiva y cuenta bloqueada ----------
_, _, E1 = pedir(EMP1)
sql(f"UPDATE password_resets SET expires_at = now() - interval '1 minute' WHERE token_hash = '{hashlib.sha256(E1.encode()).hexdigest()}'")
est, _, _ = cambiar(E1, secrets.token_urlsafe(12))
paso("BE-REC-22", "Cambiar con un enlace vencido", "400", f"{est} enlace={len(E1) == 64}", est == 400 and len(E1) == 64)
_, _, E2 = pedir(EMP1)
sql(f"UPDATE users SET active = false WHERE email = '{EMP1}'")
est, _, _ = cambiar(E2, secrets.token_urlsafe(12))
antes = pedidos(EMP1)
est2, cuerpo, _ = llamar("POST", "/api/auth/password/forgot", {"email": EMP1})
paso("BE-REC-23", "Cuenta desactivada: cambiar con un enlace pedido antes y pedir otro", "400; 200 con el mensaje general y sin pedido nuevo",
     f"{est}; {est2} igual={cuerpo == desconocido} pedido_nuevo={pedidos(EMP1) != antes}", est == 400 and est2 == 200 and cuerpo == desconocido and pedidos(EMP1) == antes)
intentos = [entra(EMP2, "mala")[0] for _ in range(6)]
bloqueada, _, _ = entra(EMP2, CLAVE_EMP)
_, _, E3 = pedir(EMP2)
CLAVE2 = secrets.token_urlsafe(12)
est, _, _ = cambiar(E3, CLAVE2)
luego, _, _ = entra(EMP2, CLAVE2)
paso("BE-REC-24", "Cuenta bloqueada por intentos fallidos: recuperar la contrasena", "el cambio responde 200 y la cuenta vuelve a ingresar",
     f"bloqueada={bloqueada} cambio={est} ingreso={luego}", est == 200 and luego == 200)
_, _, E4 = pedir(EMP2)
est, cuerpo, _ = cambiar(E4, " " * 8)
paso("BE-REC-25", "Cambiar a una contrasena de ocho espacios", "rechazo", f"{est} {str(cuerpo)[:60]}", est == 400)

# ---------- Rastro ----------
rastro = sql("SELECT action || '=' || count(*) FROM audit_log WHERE action LIKE 'password_reset%' GROUP BY action ORDER BY action").replace("\n", " ")
paso("BE-REC-26", "Los pedidos y los cambios quedan en el registro de actividad", "password_reset_requested y password_reset_completed", rastro,
     "password_reset_requested" in rastro and "password_reset_completed" in rastro)
est, cuerpo, _ = llamar("GET", "/health")
paso("BE-REC-27", "El servidor sigue respondiendo al final de la corrida", "200", est, est == 200)
print(f"TOTAL | PASA {CUENTA['PASA']} | FALLA {CUENTA['FALLA']} | DATO {CUENTA['DATO']}")
