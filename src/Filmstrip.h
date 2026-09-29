#pragma once

#include "AppState.h"

#include <thread>

// Thumbnail resolutions snap to this ladder as the filmstrip is resized.
constexpr int kFilmstripThumbEdges[] = {16, 24, 32, 48, 256, 1024};

inline int snapFilmstripThumbEdge(float devicePx) {
  for (int edge : kFilmstripThumbEdges)
    if (devicePx <= (float)edge) return edge;
  return kFilmstripThumbEdges[sizeof(kFilmstripThumbEdges) / sizeof(kFilmstripThumbEdges[0]) - 1];
}

constexpr int kMaxFilmstripTextures = 64;
constexpr int kFilmstripUploadsPerFrame = 2;

void freeFilmstripTextures(std::vector<FilmstripEntry> &entries);
void clearFilmstripThumbJobs(App &app);
void invalidateFilmstripThumbs(App &app);
void requestFilmstripThumb(App &app, int index, bool front);
void pumpFilmstripThumbs(App &app);
void refreshFilmstrip(App &app);
std::thread startFilmstripThumbThread(App *app);
