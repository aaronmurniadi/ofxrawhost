#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

struct JsonCursor {
  const char *p = nullptr;
  const char *end = nullptr;
};

static void skipWs(JsonCursor &c) {
  while (c.p < c.end && std::isspace((unsigned char)*c.p)) ++c.p;
}

static bool match(JsonCursor &c, char ch) {
  skipWs(c);
  if (c.p >= c.end || *c.p != ch) return false;
  ++c.p;
  return true;
}

static bool parseString(JsonCursor &c, std::string &out) {
  skipWs(c);
  if (c.p >= c.end || *c.p != '"') return false;
  ++c.p;
  out.clear();
  while (c.p < c.end) {
    char ch = *c.p++;
    if (ch == '"') return true;
    if (ch == '\\' && c.p < c.end) {
      char e = *c.p++;
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
    } else
      out += ch;
  }
  return false;
}

bool extractObject(const std::string &json, const char *key, std::string &objOut) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
  if (pos >= json.size() || json[pos] != '{') return false;
  int depth = 0;
  size_t start = pos;
  for (; pos < json.size(); ++pos) {
    if (json[pos] == '{') ++depth;
    else if (json[pos] == '}') {
      --depth;
      if (depth == 0) {
        objOut = json.substr(start, pos - start + 1);
        return true;
      }
    }
  }
  return false;
}

bool extractArray(const std::string &json, const char *key, std::string &arrOut) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
  if (pos >= json.size() || json[pos] != '[') return false;
  int depth = 0;
  size_t start = pos;
  for (; pos < json.size(); ++pos) {
    if (json[pos] == '[') ++depth;
    else if (json[pos] == ']') {
      --depth;
      if (depth == 0) {
        arrOut = json.substr(start, pos - start + 1);
        return true;
      }
    }
  }
  return false;
}

bool extractStringField(const std::string &json, const char *key, std::string &out) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  JsonCursor c{json.c_str() + pos + needle.size(), json.c_str() + json.size()};
  return parseString(c, out);
}

bool extractIntField(const std::string &json, const char *key, int &out) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
  char *end = nullptr;
  long v = std::strtol(json.c_str() + pos, &end, 10);
  if (end == json.c_str() + pos) return false;
  out = (int)v;
  return true;
}

bool extractFloatField(const std::string &json, const char *key, float &out) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
  char *end = nullptr;
  double v = std::strtod(json.c_str() + pos, &end);
  if (end == json.c_str() + pos) return false;
  out = (float)v;
  return true;
}

bool extractBoolField(const std::string &json, const char *key, bool &out) {
  const std::string needle = std::string("\"") + key + "\":";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos += needle.size();
  while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
  if (json.compare(pos, 4, "true") == 0) {
    out = true;
    return true;
  }
  if (json.compare(pos, 5, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

void loadGuiFromJson(const std::string &guiObj, PersistGui &g) {
  extractIntField(guiObj, "outputIndex", g.outputIndex);
  extractIntField(guiObj, "exportFormat", g.exportFormat);
  extractIntField(guiObj, "jpegQuality", g.jpegQuality);
  extractIntField(guiObj, "previewRes", g.previewRes);
  extractIntField(guiObj, "themeIndex", g.themeIndex);
  extractBoolField(guiObj, "showLeft", g.showLeft);
  extractBoolField(guiObj, "showRight", g.showRight);
  extractFloatField(guiObj, "leftW", g.leftW);
  extractFloatField(guiObj, "rightW", g.rightW);
  extractBoolField(guiObj, "showFilmstrip", g.showFilmstrip);
  extractFloatField(guiObj, "filmstripH", g.filmstripH);
}

static void parseParamsObject(const std::string &paramsObj, std::map<std::string, std::string> &params) {
  JsonCursor c{paramsObj.c_str(), paramsObj.c_str() + paramsObj.size()};
  if (!match(c, '{')) return;
  for (;;) {
    skipWs(c);
    if (c.p < c.end && *c.p == '}') return;
    std::string key;
    if (!parseString(c, key)) return;
    skipWs(c);
    if (c.p >= c.end || *c.p != ':') return;
    ++c.p;
    skipWs(c);
    if (c.p >= c.end) return;
    const char *valStart = c.p;
    if (*c.p == '"') {
      std::string s;
      parseString(c, s);
      params[key] = std::string("\"") + jsonEscape(s) + '"';
    } else if (*c.p == '{' || *c.p == '[') {
      char open = *c.p;
      char close = open == '{' ? '}' : ']';
      int depth = 0;
      do {
        if (*c.p == open) ++depth;
        else if (*c.p == close) --depth;
        ++c.p;
      } while (c.p < c.end && depth > 0);
      params[key] = std::string(valStart, c.p);
    } else {
      while (c.p < c.end && *c.p != ',' && *c.p != '}') ++c.p;
      params[key] = std::string(valStart, c.p);
    }
    skipWs(c);
    if (c.p < c.end && *c.p == ',') ++c.p;
  }
}

static void parseGroupOpen(const std::string &obj, std::map<std::string, bool> &groupOpen) {
  JsonCursor c{obj.c_str(), obj.c_str() + obj.size()};
  if (!match(c, '{')) return;
  for (;;) {
    skipWs(c);
    if (c.p < c.end && *c.p == '}') return;
    std::string key;
    if (!parseString(c, key)) return;
    skipWs(c);
    if (c.p >= c.end || *c.p != ':') return;
    ++c.p;
    skipWs(c);
    bool v = false;
    if (c.p + 4 <= c.end && std::strncmp(c.p, "true", 4) == 0) {
      v = true;
      c.p += 4;
    } else if (c.p + 5 <= c.end && std::strncmp(c.p, "false", 5) == 0)
      c.p += 5;
    groupOpen[key] = v;
    skipWs(c);
    if (c.p < c.end && *c.p == ',') ++c.p;
  }
}

bool loadChainFromJson(const std::string &chainObj, PersistChain &chain) {
  extractIntField(chainObj, "selectedNode", chain.selectedNode);
  std::string nodesArr;
  if (!extractArray(chainObj, "nodes", nodesArr)) return false;
  JsonCursor c{nodesArr.c_str(), nodesArr.c_str() + nodesArr.size()};
  if (!match(c, '[')) return false;
  chain.nodes.clear();
  for (;;) {
    skipWs(c);
    if (c.p < c.end && *c.p == ']') return true;
    if (!match(c, '{')) return false;
    const char *nodeStart = c.p - 1;
    int depth = 1;
    while (c.p < c.end && depth > 0) {
      if (*c.p == '{') ++depth;
      else if (*c.p == '}') --depth;
      ++c.p;
    }
    std::string nodeObj(nodeStart, c.p);
    PersistNode n;
    extractStringField(nodeObj, "pluginIdentifier", n.pluginIdentifier);
    extractStringField(nodeObj, "pluginLabel", n.pluginLabel);
    extractBoolField(nodeObj, "enabled", n.enabled);
    std::string go;
    if (extractObject(nodeObj, "groupOpen", go)) parseGroupOpen(go, n.groupOpen);
    std::string po;
    if (extractObject(nodeObj, "params", po)) parseParamsObject(po, n.paramsJson);
    chain.nodes.push_back(std::move(n));
    skipWs(c);
    if (c.p < c.end && *c.p == ',') ++c.p;
  }
}

bool readAllText(const std::string &path, std::string &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return true;
}

bool loadWorkspaceProject(const std::string &workspaceDir, PersistGui &gui, std::string &activeImageRel) {
  std::string json;
  if (!readAllText(workspaceProjectPath(workspaceDir), json)) return false;
  std::string guiObj;
  if (extractObject(json, "gui", guiObj)) loadGuiFromJson(guiObj, gui);
  extractStringField(json, "activeImage", activeImageRel);
  return true;
}

bool loadSidecarFile(const std::string &path, PersistSidecar &out) {
  std::string json;
  if (!readAllText(path, json)) return false;
  extractStringField(json, "kind", out.kind);
  extractStringField(json, "sourcePath", out.sourcePath);
  extractStringField(json, "inputColorSpace", out.inputColorSpace);
  extractStringField(json, "exportedAt", out.exportedAt);
  std::string guiObj;
  if (extractObject(json, "gui", guiObj)) loadGuiFromJson(guiObj, out.gui);
  std::string chainObj;
  if (extractObject(json, "chain", chainObj)) loadChainFromJson(chainObj, out.chain);
  return true;
}
