#include "core/log/callbook.h"

#ifdef GTTY_HAVE_CURL
#include <curl/curl.h>
#endif

#include <cctype>
#include <cstring>

namespace gtty {

namespace {
std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string unescape(std::string s) {
  struct E { const char* from; const char* to; };
  static const E ents[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
  for (const auto& e : ents) {
    size_t p;
    while ((p = s.find(e.from)) != std::string::npos) s.replace(p, std::strlen(e.from), e.to);
  }
  return s;
}
std::string join(std::initializer_list<std::string> parts, const char* sep) {
  std::string out;
  for (const auto& p : parts) {
    if (p.empty()) continue;
    if (!out.empty()) out += sep;
    out += p;
  }
  return out;
}
}  // namespace

std::string CallbookResult::summary() const {
  if (!found) return error;
  std::string place = join({qth, state}, " ");
  std::string s = join({name, place, country}, ", ");
  if (!grid.empty()) s += (s.empty() ? "" : " ") + ("(" + grid + ")");
  return s;
}

std::string Callbook::xmlText(const std::string& xml, const std::string& tag) {
  std::string open = "<" + tag;
  size_t p = 0;
  while ((p = xml.find(open, p)) != std::string::npos) {
    size_t q = p + open.size();
    if (q < xml.size() && (xml[q] == '>' || xml[q] == ' ')) {
      size_t gt = xml.find('>', q);
      if (gt == std::string::npos) return "";
      if (xml[gt - 1] == '/') return "";
      size_t close = xml.find("</" + tag + ">", gt + 1);
      if (close == std::string::npos) return "";
      return unescape(xml.substr(gt + 1, close - gt - 1));
    }
    p = q;
  }
  return "";
}

CallbookResult Callbook::parseQrz(const std::string& xml, std::string* sessionKey) {
  CallbookResult r;
  std::string key = xmlText(xml, "Key");
  if (sessionKey && !key.empty()) *sessionKey = key;
  std::string err = xmlText(xml, "Error");
  r.call = upper(xmlText(xml, "call"));
  if (!r.call.empty()) {
    r.found = true;
    r.name = join({xmlText(xml, "fname"), xmlText(xml, "name")}, " ");
    r.qth = xmlText(xml, "addr2");
    r.state = xmlText(xml, "state");
    r.country = xmlText(xml, "country");
    r.grid = xmlText(xml, "grid");
  } else {
    r.error = err.empty() ? (key.empty() ? "QRZ: unexpected response" : "") : "QRZ: " + err;
  }
  return r;
}

CallbookResult Callbook::parseHamQth(const std::string& xml, std::string* sessionId) {
  CallbookResult r;
  std::string id = xmlText(xml, "session_id");
  if (sessionId && !id.empty()) *sessionId = id;
  std::string err = xmlText(xml, "error");
  r.call = upper(xmlText(xml, "callsign"));
  if (!r.call.empty()) {
    r.found = true;
    r.name = xmlText(xml, "nick");
    if (r.name.empty()) r.name = xmlText(xml, "adr_name");
    r.qth = xmlText(xml, "qth");
    if (r.qth.empty()) r.qth = xmlText(xml, "adr_city");
    r.state = xmlText(xml, "us_state");
    r.country = xmlText(xml, "country");
    r.grid = xmlText(xml, "grid");
  } else {
    r.error = err.empty() ? (id.empty() ? "HamQTH: unexpected response" : "") : "HamQTH: " + err;
  }
  return r;
}

bool Callbook::available() {
#ifdef GTTY_HAVE_CURL
  return true;
#else
  return false;
#endif
}

Callbook::~Callbook() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void Callbook::configure(const CallbookConfig& cfg) {
  std::lock_guard<std::mutex> lk(mu_);
  cfg_ = cfg;
  session_.clear();
}

bool Callbook::enabled() const {
  return available() && cfg_.provider != CallbookConfig::Provider::None && !cfg_.username.empty();
}

void Callbook::lookup(const std::string& call) {
  if (!enabled()) return;
  {
    std::lock_guard<std::mutex> lk(mu_);
    pending_ = upper(call);
    if (!thread_.joinable()) thread_ = std::thread([this] { worker(); });
  }
  cv_.notify_one();
}

void Callbook::worker() {
  for (;;) {
    std::string call;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [&] { return stop_ || !pending_.empty(); });
      if (stop_) return;
      call.swap(pending_);
    }
    CallbookResult r = query(call);
    if (onResult) onResult(r);
  }
}

bool Callbook::http(const std::string& url, std::string& body, std::string& err) {
#ifdef GTTY_HAVE_CURL
  CURL* c = curl_easy_init();
  if (!c) { err = "curl init failed"; return false; }
  body.clear();
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "gtty");
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](char* p, size_t sz, size_t n, void* ud) -> size_t {
    static_cast<std::string*>(ud)->append(p, sz * n);
    return sz * n;
  });
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
  CURLcode rc = curl_easy_perform(c);
  long code = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(c);
  if (rc != CURLE_OK) { err = curl_easy_strerror(rc); return false; }
  if (code >= 400) { err = "HTTP " + std::to_string(code); return false; }
  return true;
#else
  (void)url; (void)body;
  err = "built without libcurl";
  return false;
#endif
}

CallbookResult Callbook::query(const std::string& call) {
  CallbookConfig cfg;
  std::string session;
  {
    std::lock_guard<std::mutex> lk(mu_);
    cfg = cfg_;
    session = session_;
  }
  CallbookResult r;
  r.call = call;
#ifdef GTTY_HAVE_CURL
  auto esc = [](const std::string& s) {
    char* e = curl_easy_escape(nullptr, s.c_str(), static_cast<int>(s.size()));
    std::string out = e ? e : "";
    curl_free(e);
    return out;
  };
  bool qrz = cfg.provider == CallbookConfig::Provider::Qrz;
  std::string base = qrz ? "https://xmldata.qrz.com/xml/current/?" : "https://www.hamqth.com/xml.php?";
  for (int attempt = 0; attempt < 2; ++attempt) {
    std::string body, err;
    if (session.empty()) {
      std::string url = qrz ? base + "username=" + esc(cfg.username) + "&password=" + esc(cfg.password) + "&agent=gtty"
                            : base + "u=" + esc(cfg.username) + "&p=" + esc(cfg.password);
      if (!http(url, body, err)) { r.error = "Callbook: " + err; return r; }
      std::string key;
      CallbookResult login = qrz ? parseQrz(body, &key) : parseHamQth(body, &key);
      if (key.empty()) { r.error = login.error.empty() ? "Callbook: login failed" : login.error; return r; }
      session = key;
      std::lock_guard<std::mutex> lk(mu_);
      session_ = key;
    }
    std::string url = qrz ? base + "s=" + esc(session) + "&callsign=" + esc(call)
                          : base + "id=" + esc(session) + "&callsign=" + esc(call) + "&prg=gtty";
    if (!http(url, body, err)) { r.error = "Callbook: " + err; return r; }
    std::string key;
    CallbookResult res = qrz ? parseQrz(body, &key) : parseHamQth(body, &key);
    if (res.found) { res.call = call; return res; }
    std::string e = upper(res.error);
    bool expired = e.find("SESSION") != std::string::npos || e.find("INVALID") != std::string::npos ||
                   e.find("EXPIRED") != std::string::npos || e.find("TIMEOUT") != std::string::npos;
    if (expired && attempt == 0) {
      session.clear();
      std::lock_guard<std::mutex> lk(mu_);
      session_.clear();
      continue;
    }
    res.call = call;
    if (res.error.empty()) res.error = "Not found";
    return res;
  }
#else
  (void)cfg;
  r.error = "Callbook: built without libcurl";
#endif
  return r;
}

}  // namespace gtty
