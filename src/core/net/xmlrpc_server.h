// Minimal XML-RPC over HTTP server, compatible with the clients that talk to
// fldigi (CQRLOG, TLF, Not1MM, ...). One server thread serves all connections
// side by side; the handler is invoked on that thread, one request at a time.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gtty::xmlrpc {

struct Value {
  enum class Type { Nil, Int, Double, Bool, String, Base64, Array, Struct };
  Type type = Type::Nil;
  int64_t i = 0;
  double d = 0;
  bool b = false;
  std::string s;                                   // String / Base64 (raw bytes)
  std::vector<Value> arr;
  std::vector<std::pair<std::string, Value>> members;

  static Value nil() { return Value{}; }
  static Value integer(int64_t v) { Value x; x.type = Type::Int; x.i = v; return x; }
  static Value number(double v) { Value x; x.type = Type::Double; x.d = v; return x; }
  static Value boolean(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
  static Value string(std::string v) { Value x; x.type = Type::String; x.s = std::move(v); return x; }
  static Value base64(std::string raw) { Value x; x.type = Type::Base64; x.s = std::move(raw); return x; }
  static Value array(std::vector<Value> v) { Value x; x.type = Type::Array; x.arr = std::move(v); return x; }
  static Value strct(std::vector<std::pair<std::string, Value>> m) { Value x; x.type = Type::Struct; x.members = std::move(m); return x; }

  // Lenient accessors (numbers/strings convert where sensible).
  int64_t asInt() const;
  double asDouble() const;
  bool asBool() const;
  std::string asString() const;
};

bool parseRequest(const std::string& xml, std::string& method, std::vector<Value>& params, std::string& err);
std::string serializeRequest(const std::string& method, const std::vector<Value>& params);
std::string serializeValue(const Value& v);
std::string serializeResponse(const Value& v);
std::string serializeFault(int code, const std::string& message);
std::string base64Encode(const std::string& raw);
std::string base64Decode(const std::string& text);

class Server {
 public:
  using Handler = std::function<Value(const std::string& method, const std::vector<Value>& params)>;
  Server() = default;
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  bool start(const std::string& bindAddr, uint16_t port, std::string& err);
  void stop();
  bool running() const { return fd_ >= 0; }
  void setHandler(Handler h) { handler_ = std::move(h); }
  uint64_t requestCount() const { return requests_.load(); }

 private:
  void loop();
  bool serveOne(std::string& in, std::string& out, bool& close);
  int fd_ = -1;
  std::thread thread_;
  std::atomic<bool> run_{false};
  std::atomic<uint64_t> requests_{0};
  Handler handler_;
};

}  // namespace gtty::xmlrpc
