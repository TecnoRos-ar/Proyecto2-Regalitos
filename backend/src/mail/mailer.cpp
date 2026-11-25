// src/mail/mailer.cpp
#include "mail/mailer.hpp"
#include "config.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <mutex>

namespace mail {

using njson = nlohmann::json;

static std::string provider() { return env("MAIL_PROVIDER", ""); }

bool configured() {
    auto p = provider();
    return (p == "resend" || p == "brevo") &&
           !env("MAIL_API_KEY", "").empty() &&
           !env("MAIL_FROM", "").empty();
}

// "Nombre <correo@dominio>" -> {nombre, correo}. Sin "<>": nombre vacío.
static std::pair<std::string, std::string> split_from(const std::string& from) {
    auto lt = from.find('<');
    auto gt = from.find('>', lt == std::string::npos ? 0 : lt);
    if (lt == std::string::npos || gt == std::string::npos) return {"", from};
    std::string name = from.substr(0, lt);
    while (!name.empty() && (name.back() == ' ' || name.back() == '"')) name.pop_back();
    while (!name.empty() && (name.front() == ' ' || name.front() == '"')) name.erase(0, 1);
    return {name, from.substr(lt + 1, gt - lt - 1)};
}

static size_t collect(char* ptr, size_t size, size_t n, void* out) {
    static_cast<std::string*>(out)->append(ptr, size * n);
    return size * n;
}

bool send(const Message& m, std::string& error) {
    if (!configured()) { error = "proveedor de mail no configurado"; return false; }

    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    const auto p    = provider();
    const auto key  = env("MAIL_API_KEY", "");
    const auto from = env("MAIL_FROM", "");

    std::string url;
    njson body;
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    if (p == "resend") {
        url  = env("MAIL_API_URL", "https://api.resend.com/emails");
        body = {{"from", from}, {"to", njson::array({m.to})},
                {"subject", m.subject}, {"html", m.html}, {"text", m.text}};
        headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
    } else {  // brevo
        url = env("MAIL_API_URL", "https://api.brevo.com/v3/smtp/email");
        auto [name, email] = split_from(from);
        njson sender = {{"email", email}};
        if (!name.empty()) sender["name"] = name;
        body = {{"sender", sender}, {"to", njson::array({{{"email", m.to}}})},
                {"subject", m.subject}, {"htmlContent", m.html},
                {"textContent", m.text}};
        headers = curl_slist_append(headers, ("api-key: " + key).c_str());
    }

    const std::string payload = body.dump();
    std::string response;
    CURL* c = curl_easy_init();
    if (!c) { curl_slist_free_all(headers); error = "curl_easy_init falló"; return false; }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)payload.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    curl_slist_free_all(headers);

    if (rc != CURLE_OK) { error = curl_easy_strerror(rc); return false; }
    if (status < 200 || status >= 300) {
        error = "HTTP " + std::to_string(status) + ": " + response.substr(0, 300);
        return false;
    }
    return true;
}

} // namespace mail
