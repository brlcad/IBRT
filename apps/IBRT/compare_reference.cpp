// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

#include <QImage>

#include <cstdio>
#include <cstring>
#include <vector>

#include "imagecompare.h"

namespace {

std::vector<std::uint8_t> tightRgba(const QImage &image)
{
  const std::size_t rowBytes = std::size_t(image.width()) * 4u;
  std::vector<std::uint8_t> bytes(rowBytes * std::size_t(image.height()));
  for (int y = 0; y < image.height(); ++y)
    std::memcpy(bytes.data() + std::size_t(y) * rowBytes,
        image.constScanLine(y), rowBytes);
  return bytes;
}

} // namespace

int main(int argc, char **argv)
{
  if (argc != 3) {
    std::fprintf(stderr, "usage: IBRTCompareReference expected.png actual.png\n");
    return 2;
  }

  const QImage expected = QImage(argv[1]).convertToFormat(QImage::Format_RGBA8888);
  const QImage actual = QImage(argv[2]).convertToFormat(QImage::Format_RGBA8888);
  if (expected.isNull() || actual.isNull()
      || expected.size() != actual.size()) {
    std::fprintf(stderr, "Reference image missing or dimensions differ.\n");
    return 1;
  }

  const auto expectedBytes = tightRgba(expected);
  const auto actualBytes = tightRgba(actual);
  const auto result = ibrt::imagecompare::compareRgba(expectedBytes.data(),
      actualBytes.data(), expected.width(), expected.height());
  std::printf("reference comparison: mean=%.3f max=%d fraction-over-12=%.4f\n",
      result.meanAbsError, result.maxAbsError, result.fractionOverThreshold);
  return ibrt::imagecompare::withinTolerance(result) ? 0 : 1;
}
