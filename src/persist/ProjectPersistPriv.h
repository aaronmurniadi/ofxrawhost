#pragma once

#include "persist/ProjectPersist.h"

#include <filesystem>
#include <map>
#include <sstream>
#include <string>

std::string jsonEscape(const std::string &s);
void appendGuiJson(std::ostringstream &o, const PersistGui &g);
void appendChainJson(std::ostringstream &o, const PersistChain &chain);
bool writeFile(const std::filesystem::path &path, const std::string &body);
bool extractObject(const std::string &json, const char *key, std::string &objOut);
bool extractArray(const std::string &json, const char *key, std::string &arrOut);
bool extractStringField(const std::string &json, const char *key, std::string &out);
bool extractIntField(const std::string &json, const char *key, int &out);
bool extractFloatField(const std::string &json, const char *key, float &out);
bool extractBoolField(const std::string &json, const char *key, bool &out);
void loadGuiFromJson(const std::string &guiObj, PersistGui &g);
bool loadChainFromJson(const std::string &chainObj, PersistChain &chain);
bool readAllText(const std::string &path, std::string &out);
