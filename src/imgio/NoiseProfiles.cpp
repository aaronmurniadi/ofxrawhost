#include "imgio/NoiseProfiles.h"

#include "persist/Json.h"
#include "platform/EmbeddedResource.h"
#include "platform/Paths.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// The build passes the source path, which is what a developer build reads. A
// packaged build reads the copy next to the binary (or in the bundle resources),
// and the Windows build reads the copy compiled into the executable.
#ifdef OFX_NOISE_PROFILE_PATH
constexpr const char *kBuildPath = OFX_NOISE_PROFILE_PATH;
#else
constexpr const char *kBuildPath = "";
#endif

std::vector<unsigned char> readNoiseProfileFile(std::string &location) {
  const std::vector<unsigned char> embedded = embeddedResource(kNoiseProfilesResourceId);
  if (!embedded.empty()) {
    location = "the executable";
    return embedded;
  }
  const fs::path exe = exeDir();
  const std::vector<std::string> candidates = {
      kBuildPath,
      (exe / "noiseprofiles.json").string(),
      (exe / ".." / "Resources" / "noiseprofiles.json").string(),
      "noiseprofiles.json",
  };
  for (const std::string &path : candidates) {
    if (path.empty()) continue;
    std::error_code ec;
    if (!fs::exists(path, ec)) continue;
    std::ifstream file(path, std::ios::binary);
    if (!file) continue;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) continue;
    location = path;
    return bytes;
  }
  return {};
}

// Reads one profile entry. The database holds a and b per colour channel; the
// green channel, which is the second one, is the fit the denoiser uses.
bool readProfile(const JsonValue &entry, NoiseProfile &out) {
  if (entry.kind != JsonValue::Kind::Object) return false;
  const JsonValue *iso = entry.find("iso");
  const JsonValue *a = entry.find("a");
  const JsonValue *b = entry.find("b");
  if (!iso || !a || !b) return false;
  if (a->kind != JsonValue::Kind::Array || a->arr.size() < 2) return false;
  if (b->kind != JsonValue::Kind::Array || b->arr.size() < 2) return false;
  const JsonValue &aGreen = a->arr[1];
  const JsonValue &bGreen = b->arr[1];
  if (aGreen.kind != JsonValue::Kind::Double && aGreen.kind != JsonValue::Kind::Int) return false;
  if (bGreen.kind != JsonValue::Kind::Double && bGreen.kind != JsonValue::Kind::Int) return false;

  if (iso->kind == JsonValue::Kind::Int) out.iso = (int)iso->i;
  else if (iso->kind == JsonValue::Kind::Double) out.iso = (int)iso->d;
  else return false;
  out.a = (double)aGreen.i;
  if (aGreen.kind == JsonValue::Kind::Double) out.a = aGreen.d;
  out.b = (double)bGreen.i;
  if (bGreen.kind == JsonValue::Kind::Double) out.b = bGreen.d;
  return out.iso > 0;
}

std::vector<NoiseCamera> parseNoiseProfiles(const std::vector<unsigned char> &bytes) {
  std::vector<NoiseCamera> table;
  const std::string text(bytes.begin(), bytes.end());
  JsonValue root;
  if (!JsonValue::parse(text, root)) return table;

  const JsonValue *makers = root.find("noiseprofiles");
  if (!makers || makers->kind != JsonValue::Kind::Array) return table;
  for (const JsonValue &maker : makers->arr) {
    if (maker.kind != JsonValue::Kind::Object) continue;
    const JsonValue *models = maker.find("models");
    if (!models || models->kind != JsonValue::Kind::Array) continue;
    const std::string makerName = maker.text("maker", "");
    for (const JsonValue &model : models->arr) {
      if (model.kind != JsonValue::Kind::Object) continue;
      const JsonValue *entries = model.find("profiles");
      if (!entries || entries->kind != JsonValue::Kind::Array) continue;
      NoiseCamera camera;
      camera.maker = makerName;
      camera.model = model.text("model", "");
      for (const JsonValue &entry : entries->arr) {
        // A profile darktable itself marks as unusable is left out.
        if (entry.kind == JsonValue::Kind::Object && entry.boolean("skip", false)) continue;
        NoiseProfile profile;
        if (readProfile(entry, profile)) camera.profiles.push_back(profile);
      }
      if (camera.model.empty() || camera.profiles.empty()) continue;
      std::sort(camera.profiles.begin(), camera.profiles.end(),
                [](const NoiseProfile &left, const NoiseProfile &right) { return left.iso < right.iso; });
      table.push_back(std::move(camera));
    }
  }
  return table;
}

// Comparison helpers. darktable matches a maker and a model against the database
// after its EXIF layer has already rewritten them into the database's spelling.
// LibRaw normalizes as well, but only for cameras its own table knows, so the
// matching here takes the reported spelling first and then falls back to
// comparisons that do not care about case or about a prefix the database drops
// ("Canon EOS 5D" against "EOS 5D", "NIKON D750" against "D750").
char lowerAscii(char c) {
  if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
  return c;
}

std::string toLower(const std::string &text) {
  std::string out = text;
  for (char &c : out) c = lowerAscii(c);
  return out;
}

// The database's maker appears in the reported one, which is how darktable
// matches ("Fujifilm" against "Fujifilm", "Nikon" against "Nikon Corporation").
bool makerMatches(const std::string &reported, const std::string &database) {
  if (reported.empty() || database.empty()) return false;
  return toLower(reported).find(toLower(database)) != std::string::npos;
}

// 2 when the two spellings agree, 1 when the reported model ends with the
// database's as a whole word, 0 when they do not match at all.
int modelMatchScore(const std::string &reported, const std::string &database) {
  if (reported.empty() || database.empty()) return 0;
  const std::string reportedLower = toLower(reported);
  const std::string databaseLower = toLower(database);
  if (reportedLower == databaseLower) return 2;
  if (reportedLower.size() <= databaseLower.size()) return 0;
  if (reportedLower.compare(reportedLower.size() - databaseLower.size(), databaseLower.size(), databaseLower) != 0)
    return 0;
  const char before = reportedLower[reportedLower.size() - databaseLower.size() - 1];
  if (before == ' ' || before == '-' || before == '_') return 1;
  return 0;
}

}  // namespace

const std::vector<NoiseCamera> &noiseProfileTable() {
  // Parsed once: the table is read-only afterwards, so the shared reference is
  // safe to hand out.
  static const std::vector<NoiseCamera> table = [] {
    std::string location;
    std::vector<unsigned char> bytes = readNoiseProfileFile(location);
    if (bytes.empty()) {
      std::fprintf(stderr, "warning: the bundled noise profiles were not found, camera detection is off\n");
      return std::vector<NoiseCamera>();
    }
    std::vector<NoiseCamera> parsed = parseNoiseProfiles(bytes);
    size_t entries = 0;
    for (const NoiseCamera &camera : parsed) entries += camera.profiles.size();
    // stderr, like the other diagnostics, so the line survives a build that never
    // flushes stdout.
    std::fprintf(stderr, "[profiles] %zu cameras and %zu noise profiles read from %s\n", parsed.size(), entries,
                 location.c_str());
    return parsed;
  }();
  return table;
}

bool noiseProfileLookup(const std::string &maker, const std::string &model, int iso, NoiseProfile &out) {
  if (maker.empty() || model.empty() || iso <= 0) return false;

  // The closest name wins: an exact spelling beats a case-insensitive one, which
  // beats a database name the reported model ends with. Two models of one maker
  // never relate that way, so the longer name is only a tie-break.
  const NoiseCamera *camera = nullptr;
  int bestScore = 0;
  size_t bestLength = 0;
  for (const NoiseCamera &candidate : noiseProfileTable()) {
    if (!makerMatches(maker, candidate.maker)) continue;
    const int score = modelMatchScore(model, candidate.model);
    if (score == 0) continue;
    const bool better = score > bestScore || (score == bestScore && candidate.model.size() > bestLength);
    if (!better) continue;
    camera = &candidate;
    bestScore = score;
    bestLength = candidate.model.size();
  }
  if (!camera || camera->profiles.empty()) return false;

  const NoiseProfile &lowest = camera->profiles.front();
  const NoiseProfile &highest = camera->profiles.back();
  if (iso <= lowest.iso) {
    out = lowest;
    return true;
  }
  if (iso >= highest.iso) {
    out = highest;
    return true;
  }
  for (size_t i = 1; i < camera->profiles.size(); ++i) {
    const NoiseProfile &upper = camera->profiles[i];
    if (upper.iso < iso) continue;
    if (upper.iso == iso) {
      out = upper;
      return true;
    }
    // Linear interpolation between the two neighbouring settings, as darktable
    // does for an ISO the database has no shot for.
    const NoiseProfile &lower = camera->profiles[i - 1];
    const double t = (double)(iso - lower.iso) / (double)(upper.iso - lower.iso);
    out.iso = iso;
    out.a = (1.0 - t) * lower.a + t * upper.a;
    out.b = (1.0 - t) * lower.b + t * upper.b;
    return true;
  }
  return false;
}
