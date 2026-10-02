#pragma once

// One small JSON value for the persistence layer. It replaces the per-field
// string handling that used to live in both the save and the load code.

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

struct JsonValue {
  enum class Kind { Null, Bool, Int, Double, String, Array, Object, Raw };

  Kind kind = Kind::Null;
  bool b = false;
  long long i = 0;
  double d = 0;
  std::string s;                                       // String
  std::vector<JsonValue> arr;                          // Array
  std::vector<std::pair<std::string, JsonValue>> obj;  // Object, in insertion order
  // Exact source text of a parsed value, or verbatim JSON for a Raw value.
  // dump() returns it unchanged, so a stored fragment round-trips byte for byte.
  std::string raw;

  static JsonValue makeObject() {
    JsonValue v;
    v.kind = Kind::Object;
    return v;
  }
  static JsonValue makeArray() {
    JsonValue v;
    v.kind = Kind::Array;
    return v;
  }
  static JsonValue makeString(std::string text) {
    JsonValue v;
    v.kind = Kind::String;
    v.s = std::move(text);
    return v;
  }
  static JsonValue makeInt(long long value) {
    JsonValue v;
    v.kind = Kind::Int;
    v.i = value;
    return v;
  }
  static JsonValue makeBool(bool value) {
    JsonValue v;
    v.kind = Kind::Bool;
    v.b = value;
    return v;
  }
  static JsonValue makeDouble(double value) {
    JsonValue v;
    v.kind = Kind::Double;
    v.d = value;
    return v;
  }
  // Wraps text that is already JSON, for a value the caller holds as a fragment.
  static JsonValue fromRaw(std::string text) {
    JsonValue v;
    v.kind = Kind::Raw;
    v.raw = std::move(text);
    return v;
  }

  // Adds or replaces a member of an object.
  void set(const std::string &key, JsonValue value) {
    raw.clear();  // the object is now built in memory, so no source span is valid
    for (auto &kv : obj) {
      if (kv.first == key) {
        kv.second = std::move(value);
        return;
      }
    }
    obj.emplace_back(key, std::move(value));
  }

  const JsonValue *find(const std::string &key) const {
    for (const auto &kv : obj)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }

  // Convenience readers. A missing key or a different type yields the fallback.
  long long integer(const std::string &key, long long fallback) const {
    const JsonValue *v = find(key);
    if (!v) return fallback;
    if (v->kind == Kind::Int) return v->i;
    if (v->kind == Kind::Double) return (long long)v->d;
    return fallback;
  }
  double number(const std::string &key, double fallback) const {
    const JsonValue *v = find(key);
    if (!v) return fallback;
    if (v->kind == Kind::Double) return v->d;
    if (v->kind == Kind::Int) return (double)v->i;
    return fallback;
  }
  bool boolean(const std::string &key, bool fallback) const {
    const JsonValue *v = find(key);
    return v && v->kind == Kind::Bool ? v->b : fallback;
  }
  std::string text(const std::string &key, const std::string &fallback) const {
    const JsonValue *v = find(key);
    return v && v->kind == Kind::String ? v->s : fallback;
  }

  // Compact one-line JSON with no spaces.
  std::string dump() const;

  static bool parse(const std::string &text, JsonValue &out);
};

namespace json_detail {

inline void appendQuoted(std::string &out, const std::string &text) {
  out += '"';
  for (const unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += (char)c; break;
    }
  }
  out += '"';
}

inline std::string numberText(double value) {
  std::ostringstream o;
  o.precision(17);
  o << value;
  return o.str();
}

// Recursive descent over one text buffer. Each parsed value records its own
// source span, which is what makes a fragment round-trip exactly.
class Parser {
 public:
  explicit Parser(const std::string &text) : text_(text) {}

  bool parseRoot(JsonValue &out) {
    skip();
    if (!value(out)) return false;
    skip();
    return pos_ == text_.size();
  }

 private:
  const std::string &text_;
  size_t pos_ = 0;

  void skip() {
    while (pos_ < text_.size() && std::isspace((unsigned char)text_[pos_])) ++pos_;
  }
  bool expect(char c) {
    if (pos_ >= text_.size() || text_[pos_] != c) return false;
    ++pos_;
    return true;
  }
  bool word(const char *w) {
    const size_t n = std::strlen(w);
    if (text_.compare(pos_, n, w) != 0) return false;
    pos_ += n;
    return true;
  }
  bool string(std::string &out) {
    out.clear();
    if (!expect('"')) return false;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) return false;
      const char e = text_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += e; break;
      }
    }
    return false;
  }
  bool number(JsonValue &out) {
    const size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
    bool digits = false, fractional = false;
    while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) {
      ++pos_;
      digits = true;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      fractional = true;
      ++pos_;
      while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) {
        ++pos_;
        digits = true;
      }
    }
    if (digits && pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      fractional = true;
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
      while (pos_ < text_.size() && std::isdigit((unsigned char)text_[pos_])) ++pos_;
    }
    if (!digits) return false;
    const std::string token = text_.substr(start, pos_ - start);
    out = JsonValue();
    if (fractional) {
      out.kind = JsonValue::Kind::Double;
      out.d = std::strtod(token.c_str(), nullptr);
    } else {
      out.kind = JsonValue::Kind::Int;
      out.i = std::strtoll(token.c_str(), nullptr, 10);
    }
    return true;
  }
  bool object(JsonValue &out) {
    out = JsonValue::makeObject();
    if (!expect('{')) return false;
    skip();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    for (;;) {
      skip();
      std::string key;
      if (!string(key)) return false;
      skip();
      if (!expect(':')) return false;
      skip();
      JsonValue member;
      if (!value(member)) return false;
      out.obj.emplace_back(std::move(key), std::move(member));
      skip();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      return expect('}');
    }
  }
  bool array(JsonValue &out) {
    out = JsonValue::makeArray();
    if (!expect('[')) return false;
    skip();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    for (;;) {
      skip();
      JsonValue element;
      if (!value(element)) return false;
      out.arr.push_back(std::move(element));
      skip();
      if (pos_ < text_.size() && text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      return expect(']');
    }
  }
  bool value(JsonValue &out) {
    skip();
    const size_t start = pos_;
    if (pos_ >= text_.size()) return false;
    bool ok = false;
    switch (text_[pos_]) {
      case '{': ok = object(out); break;
      case '[': ok = array(out); break;
      case '"': {
        out = JsonValue();
        out.kind = JsonValue::Kind::String;
        ok = string(out.s);
        break;
      }
      case 't':
        out = JsonValue::makeBool(true);
        ok = word("true");
        break;
      case 'f':
        out = JsonValue::makeBool(false);
        ok = word("false");
        break;
      case 'n':
        out = JsonValue();
        ok = word("null");
        break;
      default: ok = number(out); break;
    }
    if (!ok) return false;
    out.raw = text_.substr(start, pos_ - start);
    return true;
  }
};

}  // namespace json_detail

inline std::string JsonValue::dump() const {
  if (!raw.empty()) return raw;
  switch (kind) {
    case Kind::Bool: return b ? "true" : "false";
    case Kind::Int: return std::to_string(i);
    case Kind::Double: return json_detail::numberText(d);
    case Kind::String: {
      std::string out;
      json_detail::appendQuoted(out, s);
      return out;
    }
    case Kind::Array: {
      std::string out = "[";
      for (size_t k = 0; k < arr.size(); ++k) {
        if (k) out += ',';
        out += arr[k].dump();
      }
      out += ']';
      return out;
    }
    case Kind::Object: {
      std::string out = "{";
      for (size_t k = 0; k < obj.size(); ++k) {
        if (k) out += ',';
        json_detail::appendQuoted(out, obj[k].first);
        out += ':';
        out += obj[k].second.dump();
      }
      out += '}';
      return out;
    }
    case Kind::Raw: return raw;
    case Kind::Null: break;
  }
  return "null";
}

inline bool JsonValue::parse(const std::string &text, JsonValue &out) {
  json_detail::Parser parser(text);
  return parser.parseRoot(out);
}
