/**
 * @file jpeg_yuyv.h
 * @brief sensor_msgs/CompressedImage JPEG ➔ YUYV frame of the map size, or
 * the named reason it cannot be delivered (spec CAM-01, CAM-09..11).
 *
 * YUYV (V4L2_PIX_FMT_YUYV): width × height × 2 bytes, Y0 U Y1 V per pixel
 * pair, rows without padding (bytesperline = 2 × width). Samples are JFIF
 * (BT.601 full range), as stored in the JPEG; no resize, no color change.
 */
#ifndef VIPEE_JPEG_YUYV_H_
#define VIPEE_JPEG_YUYV_H_

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace vipee {

/// @brief Named reasons a camera frame is dropped (logged as DROP <reason>).
enum class DropReason {
  kNone,               ///< Delivered.
  kFormatUnsupported,  ///< FORMAT_UNSUPPORTED: format does not name jpeg.
  kDecodeError,        ///< DECODE_ERROR: corrupt JPEG or unsupported layout.
  kFrameSizeMismatch,  ///< FRAME_SIZE_MISMATCH: JPEG size != map size.
};

/**
 * @brief Decodes one JPEG to YUYV of exactly width × height.
 *
 * The size comes from the JPEG header and is checked before decoding. Only
 * 4:2:0 and 4:2:2 JPEGs decode; other subsamplings (4:4:4, grayscale...) and
 * an odd width, which YUYV cannot carry, are kDecodeError.
 * @param format CompressedImage.format; accepted when it contains "jpeg".
 * @param data JPEG bytes.
 * @param len Size of data in bytes.
 * @param width Expected width in px (from the map).
 * @param height Expected height in px (from the map).
 * @param yuyv Receives width × height × 2 bytes on kNone; untouched on any
 * drop.
 * @return DropReason::kNone, or why the frame is dropped.
 */
DropReason decodeJpegToYuyv(std::string_view format, const uint8_t* data,
                            size_t len, int width, int height,
                            std::vector<uint8_t>* yuyv);

/**
 * @brief Packs I420 planes (4:2:0) into YUYV; each chroma row serves two
 * luma rows.
 * @param y Luma plane, width × height, no padding.
 * @param u Cb plane, (width / 2) × ceil(height / 2), no padding.
 * @param v Cr plane, same layout as u.
 * @param width Width in px; must be even.
 * @param height Height in px.
 * @param yuyv Output, width × height × 2 bytes.
 */
void packI420ToYuyv(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                    int width, int height, uint8_t* yuyv);

}  // namespace vipee

#endif  // VIPEE_JPEG_YUYV_H_
