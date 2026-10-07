// Implementation for Json.h: the recursive-descent parser and the compact
// serializer. Kept out of the header so the value type stays a small interface.

#include "persist/Json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

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

std::string JsonValue::dump() const {
  if (!raw.empty()) return raw;
  switch (kind) {
    case Kind::Bool:
      if (b) return "true";
      return "false";
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

bool JsonValue::parse(const std::string &text, JsonValue &out) {
  json_detail::Parser parser(text);
  return parser.parseRoot(out);
}
