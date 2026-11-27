#!/usr/bin/env python3
"""21.81 Regalitos: guion de pruebas del backend (API HTTP).

Uso (dentro de la VM de pruebas, con el backend y la base levantados):
    BASE=http://127.0.0.1:8080 ADMIN_EMAIL=... ADMIN_PASSWORD=... python3 -I 21.81-guion-backend.py

Escribe una linea por paso: ID | paso | esperado | obtenido | PASA/FALLA/DATO.
Las claves de prueba se generan en cada corrida y no se guardan.
Necesita una base recien creada (el administrador sin segundo factor).
"""
import base64, hashlib, hmac, json, os, secrets, struct, subprocess, sys, time
import urllib.request, urllib.error

BASE = os.environ.get("BASE", "http://127.0.0.1:8080")
ADMIN_EMAIL = os.environ["ADMIN_EMAIL"]
ADMIN_PASSWORD = os.environ["ADMIN_PASSWORD"]
DB = os.environ.get("DB_CONTAINER", "regalitos-db")
CUENTA = {"PASA": 0, "FALLA": 0, "DATO": 0}


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
        cuerpo_json = {"_texto": texto[:80]}
    return resp.status, cuerpo_json, resp.headers.get("Set-Cookie") or ""


def totp(secreto, desfase=0):
    clave = base64.b32decode(secreto + "=" * (-len(secreto) % 8))
    contador = struct.pack(">Q", int(time.time()) // 30 + desfase)
    mac = hmac.new(clave, contador, hashlib.sha1).digest()
    pos = mac[-1] & 0x0F
    return "%06d" % ((struct.unpack(">I", mac[pos:pos + 4])[0] & 0x7FFFFFFF) % 1000000)


def sql(consulta):
    salida = subprocess.run(["docker", "exec", DB, "psql", "-U", "regalitos", "-d", "regalitos", "-tAc", consulta],
                            capture_output=True, text=True)
    return salida.stdout.strip()


def cookie_de(cabecera):
    return cabecera.split("session=", 1)[1].split(";", 1)[0] if "session=" in cabecera else ""


def entrar_primera_vez(email, clave):
    """Ingreso de una cuenta sin segundo factor. Devuelve (cookie, secreto, cabecera)."""
    _, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": email, "password": clave})
    tmp = (cuerpo or {}).get("tmp", "")
    _, alta, _ = llamar("POST", "/api/auth/totp/setup", {"tmp": tmp})
    secreto = (alta or {}).get("secret", "")
    _, _, cabecera = llamar("POST", "/api/auth/totp/enable", {"tmp": tmp, "code": totp(secreto)})
    return cookie_de(cabecera), secreto, cabecera


# ---------- Arranque ----------
est, cuerpo, _ = llamar("GET", "/health")
paso("BE-ARR-01", "GET /health", "200 status ok", f"{est} {cuerpo}", est == 200 and cuerpo == {"status": "ok"})
for ident, ruta in (("BE-ARR-02", "/"), ("BE-ARR-03", "/login"), ("BE-ARR-04", "/admin")):
    est, _, _ = llamar("GET", ruta)
    paso(ident, f"GET {ruta} sin sesion", "200 con la pagina", est, est == 200)
est, _, _ = llamar("GET", "/no-existe")
paso("BE-ARR-05", "GET de una ruta inexistente", "404", est, est == 404)

# ---------- Rutas protegidas sin sesion ----------
protegidas = [("GET", "/api/auth/me"), ("GET", "/api/materials"), ("POST", "/api/materials"),
              ("GET", "/api/products"), ("POST", "/api/products"), ("POST", "/api/production"),
              ("GET", "/api/movements"), ("GET", "/api/state"), ("PUT", "/api/state"),
              ("GET", "/api/admin/users"), ("POST", "/api/admin/users")]
sin_sesion = [f"{m} {r}={llamar(m, r, {} if m != 'GET' else None)[0]}" for m, r in protegidas]
paso("BE-AUT-01", "Rutas de datos sin sesion", "401 en las 11", " ".join(sin_sesion),
     all(item.endswith("=401") for item in sin_sesion))
est, _, _ = llamar("GET", "/api/auth/me", cookie="a.b.c")
paso("BE-AUT-02", "GET /api/auth/me con una cookie inventada", "401", est, est == 401)

# ---------- Ingreso del administrador ----------
est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL, "password": "clave-equivocada"})
paso("BE-AUT-03", "Ingreso con clave equivocada", "401 credenciales invalidas", f"{est} {cuerpo}", est == 401)
est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": "nadie@regalitos.local", "password": "clave-equivocada"})
paso("BE-AUT-04", "Ingreso con un correo que no existe", "401 con el mismo texto", f"{est} {cuerpo}", est == 401)
est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL})
paso("BE-AUT-05", "Ingreso sin clave", "400", est, est == 400)
est, cuerpo, _ = llamar("POST", "/api/auth/login", crudo=b"{no es json")
paso("BE-AUT-06", "Ingreso con cuerpo que no es JSON", "400", est, est == 400)
est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": 1, "password": "x"})
paso("BE-AUT-07", "Ingreso con el correo como numero", "400", f"{est} {cuerpo}", est == 400)
est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL.upper(), "password": ADMIN_PASSWORD})
paso("BE-AUT-08", "Ingreso con el correo en mayusculas", "dato: lo decide el equipo (hoy distingue mayusculas)", est, "DATO")
sql(f"UPDATE users SET failed_attempts = 0, locked_until = NULL")

est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL, "password": ADMIN_PASSWORD})
tmp_admin = (cuerpo or {}).get("tmp", "")
paso("BE-AUT-09", "Primer ingreso del administrador", "200 needs=setup", f"{est} needs={(cuerpo or {}).get('needs')}",
     est == 200 and (cuerpo or {}).get("needs") == "setup")
est, alta, _ = llamar("POST", "/api/auth/totp/setup", {"tmp": tmp_admin})
secreto_admin = (alta or {}).get("secret", "")
paso("BE-AUT-10", "Alta del segundo factor", "200 con secreto, otpauth_url y qr_svg",
     f"{est} campos={sorted((alta or {}).keys())}", est == 200 and bool(secreto_admin) and "<svg" in (alta or {}).get("qr_svg", ""))
est, _, _ = llamar("POST", "/api/auth/totp/enable", {"tmp": tmp_admin, "code": "000000"})
paso("BE-AUT-11", "Activar el segundo factor con un codigo equivocado", "401", est, est == 401)
est, cuerpo, cabecera = llamar("POST", "/api/auth/totp/enable", {"tmp": tmp_admin, "code": totp(secreto_admin)})
ADMIN = cookie_de(cabecera)
paso("BE-AUT-12", "Activar el segundo factor con el codigo correcto", "200 con cookie de sesion y rol admin",
     f"{est} rol={((cuerpo or {}).get('user') or {}).get('role')} cookie={'si' if ADMIN else 'no'}", est == 200 and bool(ADMIN))
atributos = cabecera.split(";", 1)[1].strip() if ";" in cabecera else ""
paso("BE-AUT-13", "Atributos de la cookie de sesion", "HttpOnly, SameSite y Secure", atributos,
     "HttpOnly" in atributos and "SameSite" in atributos and "Secure" in atributos)
est, cuerpo, _ = llamar("GET", "/api/auth/me", cookie=ADMIN)
ID_ADMIN = (cuerpo or {}).get("id", "")
paso("BE-AUT-14", "GET /api/auth/me con sesion", "200 con id, correo y rol", f"{est} {sorted((cuerpo or {}).keys())}", est == 200)
est, _, _ = llamar("POST", "/api/auth/totp/setup", {"tmp": tmp_admin})
paso("BE-AUT-15", "Repetir el alta del segundo factor ya activado", "409", est, est == 409)

est, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": ADMIN_EMAIL, "password": ADMIN_PASSWORD})
tmp2 = (cuerpo or {}).get("tmp", "")
paso("BE-AUT-16", "Segundo ingreso del administrador", "200 needs=totp", f"{est} needs={(cuerpo or {}).get('needs')}",
     est == 200 and (cuerpo or {}).get("needs") == "totp")
est, _, _ = llamar("GET", "/api/materials", cookie=tmp2)
paso("BE-AUT-17", "Usar el token intermedio como sesion", "401", est, est == 401)
codigo = totp(secreto_admin)
est, _, _ = llamar("POST", "/api/auth/totp/verify", {"tmp": tmp2, "code": codigo})
paso("BE-AUT-18", "Verificar el segundo factor", "200", est, est == 200)
est, _, _ = llamar("POST", "/api/auth/totp/verify", {"tmp": tmp2, "code": codigo})
paso("BE-AUT-19", "Repetir el mismo codigo y el mismo token intermedio", "401 (codigo de un solo uso)", est, est == 401)

# ---------- Usuarios ----------
CLAVE_EMP = secrets.token_urlsafe(12)
est, cuerpo, _ = llamar("POST", "/api/admin/users", {"email": "empleada@regalitos.local", "password": CLAVE_EMP, "name": "Empleada"}, ADMIN)
ID_EMP = (cuerpo or {}).get("id", "")
paso("BE-USU-01", "Crear una cuenta de empleada", "201 rol employee", f"{est} rol={(cuerpo or {}).get('role')}",
     est == 201 and (cuerpo or {}).get("role") == "employee")
est, _, _ = llamar("POST", "/api/admin/users", {"email": "empleada@regalitos.local", "password": CLAVE_EMP}, ADMIN)
paso("BE-USU-02", "Crear otra cuenta con el mismo correo", "409", est, est == 409)
est, _, _ = llamar("POST", "/api/admin/users", {"email": "corta@regalitos.local", "password": "1234567"}, ADMIN)
paso("BE-USU-03", "Crear una cuenta con clave de 7 caracteres", "400", est, est == 400)
est, _, _ = llamar("POST", "/api/admin/users", {"email": "rol@regalitos.local", "password": CLAVE_EMP, "role": "jefe"}, ADMIN)
paso("BE-USU-04", "Crear una cuenta con un rol que no existe", "400", est, est == 400)
est, cuerpo, _ = llamar("POST", "/api/admin/users", {"email": "sin arroba", "password": CLAVE_EMP}, ADMIN)
paso("BE-USU-05", "Crear una cuenta con un correo sin formato de correo", "400", est, est == 400)
est, _, _ = llamar("POST", "/api/admin/users", {"email": "tipo@regalitos.local", "password": 12345678}, ADMIN)
paso("BE-USU-06", "Crear una cuenta con la clave como numero", "400", est, est == 400)

EMP, secreto_emp, _ = entrar_primera_vez("empleada@regalitos.local", CLAVE_EMP)
est, cuerpo, _ = llamar("GET", "/api/auth/me", cookie=EMP)
paso("BE-USU-07", "Primer ingreso de la empleada", "sesion con rol employee", f"{est} rol={(cuerpo or {}).get('role')}",
     est == 200 and (cuerpo or {}).get("role") == "employee")
estados = [llamar(m, r, c, EMP)[0] for m, r, c in (("GET", "/api/admin/users", None),
           ("POST", "/api/admin/users", {"email": "x@regalitos.local", "password": CLAVE_EMP}),
           ("PUT", f"/api/admin/users/{ID_ADMIN}", {"role": "employee"}), ("DELETE", f"/api/admin/users/{ID_ADMIN}", None))]
paso("BE-USU-08", "La empleada usa las rutas de administracion", "403 en las 4", estados, estados == [403] * 4)
est, cuerpo, _ = llamar("GET", "/api/admin/users", cookie=ADMIN)
campos = sorted((cuerpo or [{}])[0].keys())
paso("BE-USU-09", "Listado de usuarios", "200 sin hash de clave ni secreto", f"{est} campos={campos}",
     est == 200 and "password_hash" not in campos and "totp_secret" not in campos)
est, _, _ = llamar("PUT", f"/api/admin/users/{ID_ADMIN}", {"role": "employee"}, ADMIN)
paso("BE-USU-10", "El administrador se quita su propio rol", "400", est, est == 400)
est, _, _ = llamar("DELETE", f"/api/admin/users/{ID_ADMIN}", cookie=ADMIN)
paso("BE-USU-11", "El administrador se borra a si mismo", "400", est, est == 400)
est, cuerpo, _ = llamar("PUT", "/api/admin/users/00000000-0000-4000-8000-000000000000", {"active": False}, ADMIN)
paso("BE-USU-12", "Modificar un usuario que no existe", "404", f"{est} {cuerpo}", est == 404)
est, _, _ = llamar("PUT", "/api/admin/users/no-es-uuid", {"active": False}, ADMIN)
paso("BE-USU-13", "Modificar un usuario con un id invalido", "400", est, est == 400)

# ---------- Bloqueo por intentos ----------
intentos = [llamar("POST", "/api/auth/login", {"email": "empleada@regalitos.local", "password": "mala"})[0] for _ in range(6)]
paso("BE-BLO-01", "Seis ingresos con clave equivocada", "401 x5 y 429 en el sexto", intentos, intentos == [401] * 5 + [429])
est, _, _ = llamar("POST", "/api/auth/login", {"email": "empleada@regalitos.local", "password": CLAVE_EMP})
paso("BE-BLO-02", "Clave correcta con la cuenta bloqueada", "429", est, est == 429)
est, _, _ = llamar("GET", "/api/auth/me", cookie=EMP)
paso("BE-BLO-03", "La sesion abierta antes del bloqueo sigue activa", "200 (dato: el bloqueo no cierra sesiones)", est, "DATO")
sql("UPDATE users SET locked_until = now() - interval '1 minute' WHERE email = 'empleada@regalitos.local'")
est1, _, _ = llamar("POST", "/api/auth/login", {"email": "empleada@regalitos.local", "password": "mala"})
est2, _, _ = llamar("POST", "/api/auth/login", {"email": "empleada@regalitos.local", "password": CLAVE_EMP})
paso("BE-BLO-04", "Vencido el bloqueo: un solo error y despues la clave correcta", "401 y 200 (cinco intentos nuevos)",
     f"{est1} y {est2}", est1 == 401 and est2 == 200)
sql("UPDATE users SET failed_attempts = 0, locked_until = NULL")

# ---------- Materiales ----------
est, hilo, _ = llamar("POST", "/api/materials", {"name": "Hilo", "unit": "m", "current_stock": 10,
                                                "last_purchase_price": 1000, "last_purchase_qty": 50}, ADMIN)
hilo = hilo or {}
paso("BE-MAT-01", "Crear material Hilo (1000 por 50 m, stock 10)", "201 unit_cost 20", f"{est} unit_cost={hilo.get('unit_cost')}",
     est == 201 and hilo.get("unit_cost") == 20)
est, dije, _ = llamar("POST", "/api/materials", {"name": "Dije", "unit": "un", "current_stock": 4,
                                                "last_purchase_price": 300, "last_purchase_qty": 3}, ADMIN)
dije = dije or {}
paso("BE-MAT-02", "Crear material Dije (300 por 3 un)", "201 unit_cost 100", f"{est} unit_cost={dije.get('unit_cost')}",
     est == 201 and dije.get("unit_cost") == 100)
malos = [("BE-MAT-03", "sin nombre", {"unit": "m"}), ("BE-MAT-04", "unidad kg", {"name": "X", "unit": "kg"}),
         ("BE-MAT-05", "stock negativo", {"name": "X", "current_stock": -1}),
         ("BE-MAT-06", "precio negativo", {"name": "X", "last_purchase_price": -5}),
         ("BE-MAT-07", "cantidad comprada 0", {"name": "X", "last_purchase_price": 10, "last_purchase_qty": 0}),
         ("BE-MAT-08", "stock como texto", {"name": "X", "current_stock": "diez"}),
         ("BE-MAT-09", "nombre de 121 caracteres", {"name": "n" * 121}),
         ("BE-MAT-10", "nombre como numero", {"name": 5}),
         ("BE-MAT-11", "nombre de solo espacios", {"name": "   "})]
for ident, texto, cuerpo_malo in malos:
    est, cuerpo, _ = llamar("POST", "/api/materials", cuerpo_malo, ADMIN)
    paso(ident, f"Crear material: {texto}", "400 con mensaje", f"{est} {json.dumps(cuerpo, ensure_ascii=False)[:90]}", est == 400)
est, _, _ = llamar("GET", "/health")
paso("BE-MAT-12", "El servicio sigue respondiendo despues de los datos invalidos", "200", est, est == 200)
est, cuerpo, _ = llamar("PUT", f"/api/materials/{hilo.get('id')}", {"current_stock": 12.5}, ADMIN)
paso("BE-MAT-13", "Ajustar el stock de Hilo a 12,5", "200 current_stock 12.5", f"{est} {(cuerpo or {}).get('current_stock')}",
     est == 200 and (cuerpo or {}).get("current_stock") == 12.5)
est, movs, _ = llamar("GET", "/api/movements", cookie=ADMIN)
paso("BE-MAT-14", "El ajuste de stock queda en los movimientos", "1 movimiento de ajuste", f"{est} movimientos={len(movs or [])}",
     est == 200 and len(movs or []) == 1)
est, _, _ = llamar("PUT", f"/api/materials/{hilo.get('id')}", {"current_stock": -3}, ADMIN)
paso("BE-MAT-15", "Ajustar el stock a un valor negativo", "400", est, est == 400)
est, _, _ = llamar("PUT", "/api/materials/00000000-0000-4000-8000-000000000000", {"name": "Y"}, ADMIN)
paso("BE-MAT-16", "Modificar un material que no existe", "404", est, est == 404)
est, _, _ = llamar("PUT", f"/api/materials/{hilo.get('id')}", {"name": ""}, ADMIN)
paso("BE-MAT-17", "Dejar un material sin nombre", "400", est, est == 400)
est, lista, _ = llamar("GET", "/api/materials", cookie=EMP)
paso("BE-MAT-18", "Listado de materiales", "200 con Dije y Hilo en orden", f"{est} {[m.get('name') for m in lista or []]}",
     est == 200 and [m.get("name") for m in lista or []][:2] == ["Dije", "Hilo"])

# ---------- Productos ----------
receta = [{"material_id": hilo.get("id"), "quantity": 0.5}, {"material_id": dije.get("id"), "quantity": 1}]
est, pulsera, _ = llamar("POST", "/api/products", {"name": "Clasica", "labor_cost": 200, "other_cost": 90,
                                                  "profit_type": "pct", "profit_value": 100, "recipe": receta}, ADMIN)
pulsera = pulsera or {}
paso("BE-PRO-01", "Crear producto Clasica con receta de 2 materiales", "201", est, est == 201)
est, lista, _ = llamar("GET", "/api/products", cookie=ADMIN)
recetas = {p.get("name"): p.get("recipe") for p in lista or []}
paso("BE-PRO-02", "Listado de productos con su receta", "Clasica con 2 renglones", f"{est} renglones={len(recetas.get('Clasica') or [])}",
     est == 200 and len(recetas.get("Clasica") or []) == 2)
est, doble, _ = llamar("POST", "/api/products", {"name": "Doble", "recipe": [
    {"material_id": hilo.get("id"), "quantity": 3}, {"material_id": hilo.get("id"), "quantity": 4}]}, ADMIN)
_, lista, _ = llamar("GET", "/api/products", cookie=ADMIN)
cantidades = [r.get("quantity") for p in lista or [] if p.get("name") == "Doble" for r in p.get("recipe", [])]
paso("BE-PRO-03", "Receta con el mismo material dos veces (3 y 4)", "400, o un renglon de 7", f"{est} cantidades={cantidades}",
     est == 400 or cantidades == [7])
malos = [("BE-PRO-04", "sin nombre", {"labor_cost": 1}), ("BE-PRO-05", "tipo de ganancia invalido", {"name": "X", "profit_type": "otro"}),
         ("BE-PRO-06", "mano de obra negativa", {"name": "X", "labor_cost": -1}),
         ("BE-PRO-07", "ganancia negativa", {"name": "X", "profit_value": -10}),
         ("BE-PRO-08", "receta con cantidad 0", {"name": "X", "recipe": [{"material_id": hilo.get("id"), "quantity": 0}]}),
         ("BE-PRO-09", "receta con un material que no existe", {"name": "X", "recipe": [
             {"material_id": "00000000-0000-4000-8000-000000000000", "quantity": 1}]}),
         ("BE-PRO-10", "receta que no es una lista", {"name": "X", "recipe": "hilo"}),
         ("BE-PRO-11", "costo como texto", {"name": "X", "labor_cost": "cien"})]
for ident, texto, cuerpo_malo in malos:
    est, cuerpo, _ = llamar("POST", "/api/products", cuerpo_malo, ADMIN)
    paso(ident, f"Crear producto: {texto}", "400 con mensaje", f"{est} {json.dumps(cuerpo, ensure_ascii=False)[:90]}", est == 400)
_, lista, _ = llamar("GET", "/api/products", cookie=ADMIN)
sobrantes = sorted(p.get("name") for p in lista or [] if p.get("name") == "X")
paso("BE-PRO-12", "Los pedidos rechazados no dejan productos creados", "ningun producto X", sobrantes, sobrantes == [])
estados = [llamar(m, f"/api/products/{pulsera.get('id')}", c, ADMIN)[0] for m, c in (("PUT", {"name": "Clasica 2"}), ("DELETE", None))]
paso("BE-PRO-13", "Modificar y borrar un producto", "dato: la API no tiene esas rutas", estados, "DATO")

# ---------- Produccion ----------
est, cuerpo, _ = llamar("POST", "/api/production", {"product_id": pulsera.get("id"), "quantity": 2}, EMP)
paso("BE-PRD-01", "Armar 2 Clasica", "201 produced 2", f"{est} {cuerpo}", est == 201 and (cuerpo or {}).get("produced") == 2)
_, lista, _ = llamar("GET", "/api/materials", cookie=ADMIN)
stock = {m.get("name"): m.get("current_stock") for m in lista or []}
paso("BE-PRD-02", "Stock de materiales despues de armar 2", "Hilo 11.5 y Dije 2", stock, stock.get("Hilo") == 11.5 and stock.get("Dije") == 2)
_, lista, _ = llamar("GET", "/api/products", cookie=ADMIN)
terminado = {p.get("name"): p.get("finished_stock") for p in lista or []}.get("Clasica")
paso("BE-PRD-03", "Stock terminado de Clasica", "2", terminado, terminado == 2)
_, movs, _ = llamar("GET", "/api/movements", cookie=ADMIN)
produccion = sorted(m.get("delta") for m in movs or [] if m.get("kind") == "production")
paso("BE-PRD-04", "Movimientos de la produccion", "-2 de Dije y -1 de Hilo", produccion, produccion == [-2, -1])
snap = sql("SELECT cost_snapshot::text FROM production_orders ORDER BY created_at DESC LIMIT 1")
try:
    snap_json = json.loads(snap)
except ValueError:
    snap_json = {}
paso("BE-PRD-05", "Costos guardados en la orden (materiales 110, extra 290, costo 400, precio 800)",
     "materials_cost 110, total_cost 400, unit_price 800", snap,
     snap_json.get("materials_cost") == 110 and snap_json.get("total_cost") == 400 and snap_json.get("unit_price") == 800)
est, cuerpo, _ = llamar("POST", "/api/production", {"product_id": pulsera.get("id"), "quantity": 3}, EMP)
paso("BE-PRD-06", "Armar 3 con Dije para 2", "409 stock insuficiente de Dije", f"{est} {cuerpo}",
     est == 409 and "Dije" in str(cuerpo))
_, lista, _ = llamar("GET", "/api/materials", cookie=ADMIN)
stock = {m.get("name"): m.get("current_stock") for m in lista or []}
paso("BE-PRD-07", "El rechazo no descuenta stock", "Hilo 11.5 y Dije 2", stock, stock.get("Hilo") == 11.5 and stock.get("Dije") == 2)
for ident, texto, cantidad in (("BE-PRD-08", "0", 0), ("BE-PRD-09", "-1", -1), ("BE-PRD-10", "1,9", 1.9),
                               ("BE-PRD-11", 'el texto "1"', "1"), ("BE-PRD-12", "10000000000", 10000000000)):
    est, cuerpo, _ = llamar("POST", "/api/production", {"product_id": pulsera.get("id"), "quantity": cantidad}, EMP)
    paso(ident, f"Armar con cantidad {texto}", "400 cantidad invalida", f"{est} {json.dumps(cuerpo, ensure_ascii=False)[:80]}", est == 400)
est, cuerpo, _ = llamar("POST", "/api/production", {"product_id": "00000000-0000-4000-8000-000000000000", "quantity": 1}, EMP)
paso("BE-PRD-13", "Armar un producto que no existe", "404", f"{est} {cuerpo}", est == 404)
est, _, _ = llamar("DELETE", f"/api/materials/{dije.get('id')}", cookie=ADMIN)
paso("BE-PRD-14", "Borrar un material usado en una receta", "409", est, est == 409)
est, _, _ = llamar("GET", "/health")
paso("BE-PRD-15", "El servicio sigue respondiendo", "200", est, est == 200)

# ---------- Estado de la pantalla ----------
est, cuerpo, _ = llamar("GET", "/api/state", cookie=ADMIN)
paso("BE-EST-01", "GET /api/state en una base nueva", "200 data null", f"{est} {cuerpo}", est == 200 and cuerpo == {"data": None})
estado_app = {"materiales": [{"id": "m1", "nombre": "Hilo"}], "productos": []}
est, _, _ = llamar("PUT", "/api/state", estado_app, ADMIN)
_, cuerpo, _ = llamar("GET", "/api/state", cookie=EMP)
paso("BE-EST-02", "Guardar el estado y leerlo desde otra cuenta", "200 y el mismo estado", f"{est} {cuerpo}",
     est == 200 and (cuerpo or {}).get("data") == estado_app)
est, _, _ = llamar("PUT", "/api/state", {"materiales": "x", "productos": []}, ADMIN)
paso("BE-EST-03", "Guardar un estado con forma invalida", "400", est, est == 400)
est, _, _ = llamar("PUT", "/api/state", crudo=b'{"materiales":[],"productos":[],"x":"' + b"a" * (2 * 1024 * 1024) + b'"}', cookie=ADMIN)
paso("BE-EST-04", "Guardar un estado de mas de 2 MB", "413", est, est == 413)
_, lista, _ = llamar("GET", "/api/materials", cookie=ADMIN)
paso("BE-EST-05", "Los materiales guardados desde la pantalla aparecen en /api/materials",
     "el Hilo de la pantalla y el de la API son el mismo dato", f"pantalla=1 material; API={len(lista or [])} materiales, sin relacion", "DATO")
est, _, _ = llamar("PUT", "/api/state", {"materiales": [], "productos": []}, EMP)
_, cuerpo, _ = llamar("GET", "/api/state", cookie=ADMIN)
paso("BE-EST-06", "La empleada reemplaza todo el estado por uno vacio", "dato: no hay permisos por rol sobre los datos",
     f"{est} queda={cuerpo}", "DATO")
malicioso = {"materiales": [{"id": '"><img src=x onerror=window.__qa=1>', "nombre": "<b>x</b>", "unidad": "m",
                             "stock": 1, "precio": 1, "cantidad": 1}], "productos": []}
est, _, _ = llamar("PUT", "/api/state", malicioso, EMP)
paso("BE-EST-07", "Guardar un material cuyo id contiene HTML", "400, o se guarda y la pantalla lo muestra como texto",
     f"{est} (se comprueba en el navegador)", "DATO")

# ---------- Sesiones y permisos ----------
est, _, _ = llamar("DELETE", f"/api/materials/{hilo.get('id')}", cookie=EMP)
paso("BE-ROL-01", "La empleada borra un material", "dato: lo decide el equipo (hoy cualquier cuenta puede)", est, "DATO")
est1, _, cabecera = llamar("POST", "/api/auth/logout", cookie=EMP)
est2, _, _ = llamar("GET", "/api/auth/me", cookie=EMP)
paso("BE-SES-01", "Cerrar sesion y volver a usar la cookie anterior", "200 y despues 401", f"{est1} y {est2}", est1 == 200 and est2 == 401)
est, _, _ = llamar("PUT", f"/api/admin/users/{ID_EMP}", {"active": False}, ADMIN)
est2, _, _ = llamar("GET", "/api/auth/me", cookie=EMP)
est3, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": "empleada@regalitos.local", "password": CLAVE_EMP})
paso("BE-SES-02", "Desactivar a la empleada", "200; su sesion responde 401 y no puede ingresar", f"{est}; sesion {est2}; ingreso {est3}",
     est == 200 and est2 == 401 and est3 in (401, 403))
llamar("PUT", f"/api/admin/users/{ID_EMP}", {"active": True}, ADMIN)
est, _, _ = llamar("PUT", f"/api/admin/users/{ID_EMP}", {"reset_2fa": True}, ADMIN)
est2, cuerpo, _ = llamar("POST", "/api/auth/login", {"email": "empleada@regalitos.local", "password": CLAVE_EMP})
paso("BE-SES-03", "Reiniciar el segundo factor de la empleada", "200 y el ingreso pide configurarlo de nuevo",
     f"{est} needs={(cuerpo or {}).get('needs')}", est == 200 and (cuerpo or {}).get("needs") == "setup")
est, _, _ = llamar("DELETE", f"/api/admin/users/{ID_EMP}", cookie=ADMIN)
paso("BE-SES-04", "Borrar a la empleada, que tiene ordenes de produccion", "409 con el aviso de desactivar", est, est == 409)
registrados = sql("SELECT count(*) FROM audit_log")
paso("BE-SES-05", "Los cambios de usuarios quedan registrados", "al menos 4 registros", registrados, registrados.isdigit() and int(registrados) >= 4)
est, _, _ = llamar("GET", "/health")
paso("BE-FIN-01", "El servicio termina la corrida respondiendo", "200", est, est == 200)

print(f"TOTAL | PASA {CUENTA['PASA']} | FALLA {CUENTA['FALLA']} | DATO {CUENTA['DATO']}")
