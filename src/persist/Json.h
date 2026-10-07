#pragma once

// One small JSON value for the persistence layer. It replaces the per-field
// string handling that used to live in both the save and the load code.

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
