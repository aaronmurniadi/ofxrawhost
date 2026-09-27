#pragma once

#include "AppState.h"

#include <thread>

constexpr int kFilmstripThumbEdge = 128;
constexpr int kMaxFilmstripTextures = 64;
constexpr int kFilmstripUploadsPerFrame = 2;

void freeFilmstripTextures(std::vector<FilmstripEntry> &entries);
void clearFilmstripThumbJobs(App &app);
void requestFilmstripThumb(App &app, int index, bool front);
void pumpFilmstripThumbs(App &app);
void refreshFilmstrip(App &app);
std::thread startFilmstripThumbThread(App *app);
