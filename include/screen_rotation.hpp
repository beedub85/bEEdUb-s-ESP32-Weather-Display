#pragma once

#include <algorithm>

namespace screenRotation {
constexpr int width = 800;
constexpr int height = 480;

constexpr int flipX(int x) { return width - 1 - x; }
constexpr int flipY(int y) { return height - 1 - y; }

template<class Pixel>
void copyTile(const Pixel *source, Pixel *destination, int count) {
  std::reverse_copy(source, source + count, destination);
}
}
