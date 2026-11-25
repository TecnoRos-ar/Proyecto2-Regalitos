// src/mail/mailer.hpp
#pragma once
#include <string>

namespace mail {

struct Message {
    std::string to;
    std::string subject;
    std::string html;
    std::string text;
};

// true si hay un proveedor configurado (MAIL_PROVIDER + MAIL_API_KEY + MAIL_FROM).
bool configured();

// Envía por la API HTTPS del proveedor (no usa SMTP: Render y otros bloquean
// esos puertos). Devuelve true si el proveedor aceptó el mail.
// Variables de entorno:
//   MAIL_PROVIDER  resend | brevo
//   MAIL_API_KEY   clave de API del proveedor
//   MAIL_FROM      remitente, ej: "Regalitos <no-reply@tudominio.com>"
//   MAIL_API_URL   (opcional) reemplaza la URL de la API, para pruebas
bool send(const Message& m, std::string& error);

} // namespace mail
