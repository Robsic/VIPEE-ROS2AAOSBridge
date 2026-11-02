#include "vipee/jpeg_yuyv.h"

#include <turbojpeg.h>

#include <memory>

namespace vipee {
namespace {

// chroma_rows_per_row: 2 for 4:2:0 (one chroma row per two luma rows), 1 for
// 4:2:2.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void packToYuyv(const uint8_t* y, const uint8_t* u, const uint8_t* v, int width,
                int height, int chroma_rows_per_row, uint8_t* yuyv) {
  const size_t cw = static_cast<size_t>(width) / 2;
  for (int row = 0; row < height; ++row) {
    const uint8_t* yr = y + static_cast<size_t>(row) * width;
    const size_t c = static_cast<size_t>(row / chroma_rows_per_row) * cw;
    uint8_t* out = yuyv + static_cast<size_t>(row) * width * 2;
    for (size_t i = 0; i < cw; ++i) {
      out[4 * i] = yr[2 * i];
      out[4 * i + 1] = u[c + i];
      out[4 * i + 2] = yr[2 * i + 1];
      out[4 * i + 3] = v[c + i];
    }
  }
}

}  // namespace

void packI420ToYuyv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                    int width, int height, uint8_t* yuyv) {
  packToYuyv(y, u, v, width, height, 2, yuyv);
}

DropReason decodeJpegToYuyv(
    std::string_view format, const uint8_t* data,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    size_t len, int width, int height, std::vector<uint8_t>* yuyv) {
  if (format.find("jpeg") == std::string_view::npos) {
    return DropReason::kFormatUnsupported;
  }
  // ponytail: one TurboJPEG handle per frame; keep one per camera if the
  // STATS latency shows the init cost.
  std::unique_ptr<void, decltype(&tj3Destroy)> tj(tj3Init(TJINIT_DECOMPRESS),
                                                  &tj3Destroy);
  if (!tj || data == nullptr || len == 0 ||
      tj3DecompressHeader(tj.get(), data, len) != 0) {
    return DropReason::kDecodeError;
  }
  if (tj3Get(tj.get(), TJPARAM_JPEGWIDTH) != width ||
      tj3Get(tj.get(), TJPARAM_JPEGHEIGHT) != height) {
    return DropReason::kFrameSizeMismatch;
  }
  const int subsamp = tj3Get(tj.get(), TJPARAM_SUBSAMP);
  if ((subsamp != TJSAMP_420 && subsamp != TJSAMP_422) || width % 2 != 0) {
    return DropReason::kDecodeError;
  }
  const int rows_per_chroma = subsamp == TJSAMP_420 ? 2 : 1;
  const size_t luma = static_cast<size_t>(width) * height;
  const size_t chroma = static_cast<size_t>(width / 2) *
                        ((height + rows_per_chroma - 1) / rows_per_chroma);
  std::vector<uint8_t> planes(luma + 2 * chroma);
  unsigned char* dst[3] = {planes.data(), planes.data() + luma,
                           planes.data() + luma + chroma};
  int strides[3] = {width, width / 2, width / 2};
  // Any libjpeg warning (e.g. premature end of data) also returns -1.
  if (tj3DecompressToYUVPlanes8(tj.get(), data, len, dst, strides) != 0) {
    return DropReason::kDecodeError;
  }
  yuyv->resize(luma * 2);
  packToYuyv(dst[0], dst[1], dst[2], width, height, rows_per_chroma,
             yuyv->data());
  return DropReason::kNone;
}

}  // namespace vipee
