#pragma once

#include <cstdint>

namespace wot {

// The two-word history updated by Chinese HD #1513 at 0x00ac20a0.
// Query ownership, scene epochs and admission belong to the renderer.
class StockSceneOcclusionHistory {
public:
  constexpr StockSceneOcclusionHistory(std::uint32_t frame = 0,
                                      std::uint32_t packed = 0) noexcept
      : frame_(frame), packed_(packed) {}

  constexpr std::uint32_t last_frame() const noexcept { return frame_; }
  constexpr std::uint32_t packed() const noexcept { return packed_; }

  void reset() noexcept {
    frame_ = 0;
    packed_ = 0;
  }

  void observe(std::uint32_t frame, bool visible,
               bool reset_gap = true) noexcept {
    if (reset_gap && std::uint32_t(frame - frame_) > 1)
      packed_ &= 0x80000000u;
    frame_ = frame;
    if (visible) {
      const auto count = (packed_ >> 16) & 0x7fffu;
      packed_ = (packed_ & 0x80000000u) |
                ((count == 0x7fffu ? count : count + 1) << 16);
    } else {
      const auto count = packed_ & 0xffffu;
      packed_ = (packed_ & 0x80000000u) |
                (count == 0xffffu ? count : count + 1);
    }
  }

  // A nonblocking renderer can retain a pending query across frames. An old
  // zero-pixel result must not hide an object indefinitely while it is pending.
  bool draw(std::uint32_t frame, bool near_plane,
            bool query_admitted) noexcept {
    if (near_plane) {
      observe(frame, true);
      return true;
    }
    return !query_admitted || std::uint32_t(frame - frame_) > 1 ||
           (packed_ & 0xffffu) == 0;
  }

private:
  std::uint32_t frame_;
  std::uint32_t packed_;
};

} // namespace wot
