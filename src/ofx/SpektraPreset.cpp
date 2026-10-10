#include "ofx/SpektraPreset.h"

#include "ofx/OfxTypes.h"
#include "persist/Json.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

// The plugin XORs the snapshot with this key plus a positional term. The
// obfuscation is its own inverse, so the same loop encodes and decodes.
constexpr char kKey[] = "SpektraFilmOFX1!";
constexpr size_t kKeyLen = sizeof(kKey) - 1;

// Snapshot parameter kinds, as written by the plugin. The numeric values are
// part of the file format and must not be renumbered.
enum class SnapshotKind : int { Int = 1, Bool = 2, Double = 3, Double2D = 4, Double3D = 5 };

int snapshotComponents(SnapshotKind kind) {
  if (kind == SnapshotKind::Double2D) return 2;
  if (kind == SnapshotKind::Double3D) return 3;
  return 1;
}

bool snapshotUsesDouble(SnapshotKind kind) {
  return kind == SnapshotKind::Double || kind == SnapshotKind::Double2D || kind == SnapshotKind::Double3D;
}

// A snapshot kind fits a host parameter when both carry the same number of
// scalar components. The integer kinds cover the host's int, bool, and choice.
bool kindFits(SnapshotKind kind, ParamType type) {
  switch (kind) {
    case SnapshotKind::Int:
      return type == ParamType::Integer || type == ParamType::Boolean || type == ParamType::Choice;
    case SnapshotKind::Bool:
      return type == ParamType::Boolean || type == ParamType::Integer;
    case SnapshotKind::Double:
      return type == ParamType::Double;
    case SnapshotKind::Double2D:
      return type == ParamType::Double2D;
    case SnapshotKind::Double3D:
      return type == ParamType::Double3D;
  }
  return false;
}

bool hexNibble(char c, uint8_t &out) {
  if (c >= '0' && c <= '9') {
    out = (uint8_t)(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = (uint8_t)(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = (uint8_t)(c - 'A' + 10);
    return true;
  }
  return false;
}

bool hexDecode(const std::string &text, std::string &out) {
  if (text.size() % 2u != 0u) return false;
  out.clear();
  out.reserve(text.size() / 2u);
  for (size_t i = 0; i < text.size(); i += 2u) {
    uint8_t high = 0;
    uint8_t low = 0;
    if (!hexNibble(text[i], high) || !hexNibble(text[i + 1u], low)) return false;
    out.push_back((char)(uint8_t)((high << 4u) | low));
  }
  return true;
}

void deobfuscate(std::string &text) {
  for (size_t i = 0; i < text.size(); ++i) {
    const uint8_t stream = (uint8_t)((uint8_t)kKey[i % kKeyLen] + (uint8_t)((i * 37u) & 0xffu));
    text[i] = (char)(uint8_t)((uint8_t)text[i] ^ stream);
  }
}

bool readFile(const std::string &path, std::string &text, std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "could not open " + path;
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return true;
}

double scalarFromRow(std::istringstream &row, bool asDouble, bool &ok) {
  if (asDouble) {
    double value = 0;
    if (!(row >> value)) {
      ok = false;
      return 0;
    }
    return value;
  }
  int value = 0;
  if (!(row >> value)) {
    ok = false;
    return 0;
  }
  return (double)value;
}

// Applies one "name kind components values..." snapshot line. Returns the
// parameter when it exists, fits the kind, and the row parsed completely.
Param *applyLine(const std::string &line, Effect *effect) {
  std::istringstream row(line);
  std::string name;
  int kindRaw = 0;
  int components = 0;
  if (!(row >> name >> kindRaw >> components)) return nullptr;

  const SnapshotKind kind = (SnapshotKind)kindRaw;
  if (components != snapshotComponents(kind)) return nullptr;

  Param *p = findParam(effect, name.c_str());
  if (!p) return nullptr;
  // The host drives the color-space parameters. A preset must not move them, or
  // the host and the plugin would disagree about the input and output spaces.
  const std::string label = sprop(p->props, kOfxPropLabel);
  if (label == kInputColorSpaceLabel || label == kOutputColorSpaceLabel) return nullptr;
  if (!kindFits(kind, p->kind)) return nullptr;
  if (p->v.size() < (size_t)components) return nullptr;

  const bool asDouble = snapshotUsesDouble(kind);
  bool ok = true;
  double values[3] = {0, 0, 0};
  for (int i = 0; i < components; ++i) {
    values[i] = scalarFromRow(row, asDouble, ok);
    if (!ok) return nullptr;
  }

  std::lock_guard<std::mutex> lock(gValueMutex);
  for (int i = 0; i < components; ++i) p->v[i] = values[i];
  return p;
}

}  // namespace

bool isSpektrafilmPlugin(const char *pluginIdentifier) {
  if (!pluginIdentifier) return false;
  return std::string(pluginIdentifier).compare(0, 15, "org.spektrafilm") == 0;
}

std::vector<Param *> applySpektrafilmPreset(const std::string &path, Effect *effect, std::string &error) {
  error.clear();
  std::vector<Param *> changed;
  if (!effect) {
    error = "no plugin instance to apply the preset to";
    return changed;
  }

  std::string text;
  if (!readFile(path, text, error)) return changed;

  JsonValue root;
  if (!JsonValue::parse(text, root) || root.kind != JsonValue::Kind::Object) {
    error = "the file is not valid JSON";
    return changed;
  }
  if (root.text("format", "") != "spektrafilm-preset-v1") {
    error = "the file is not a spektrafilm preset";
    return changed;
  }
  if (root.text("payload_encoding", "") != "obfuscated-snapshot-hex") {
    error = "the preset uses an unsupported payload encoding";
    return changed;
  }

  std::string payload;
  if (!hexDecode(root.text("payload_hex", ""), payload)) {
    error = "the preset snapshot payload is invalid";
    return changed;
  }
  deobfuscate(payload);

  std::istringstream input(payload);
  std::string header;
  if (!std::getline(input, header) || header.compare(0, 7, "SPKDFLT") != 0) {
    error = "the preset snapshot is not a recognized parameter snapshot";
    return changed;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    Param *p = applyLine(line, effect);
    if (p) changed.push_back(p);
  }
  return changed;
}
