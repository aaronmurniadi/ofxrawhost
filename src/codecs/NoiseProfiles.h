// Sensor noise profiles: the table darktable fits to every camera and ISO, and
// the lookup that turns the camera and ISO of the open frame into the a and b of
// the noise model V(X) = a * X + b.
#pragma once

#include <string>
#include <vector>

// One ISO setting of one camera, green channel of the fit, which is the channel
// the denoiser builds its model on.
struct NoiseProfile {
  int iso = 0;
  double a = 0.0;
  double b = 0.0;
};

// Every ISO setting of one camera, sorted by ISO.
struct NoiseCamera {
  std::string maker;
  std::string model;
  std::vector<NoiseProfile> profiles;
};

// The bundled table, parsed on first use from the copy of darktable's
// noiseprofiles.json that ships with the app. Empty when that copy cannot be
// read, and then lookups simply fail.
const std::vector<NoiseCamera> &noiseProfileTable();

// The profile for one camera and ISO. The maker matches when the database's
// maker string appears in the reported one, and the model has to match exactly,
// which is how darktable matches as well. An ISO that falls between two entries
// is linearly interpolated; one outside the table clamps to the nearest entry.
// Returns false when the camera (or its model) is not in the table at all.
bool noiseProfileLookup(const std::string &maker, const std::string &model, int iso, NoiseProfile &out);
