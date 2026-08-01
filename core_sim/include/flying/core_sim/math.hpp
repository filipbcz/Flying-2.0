#pragma once

#include <cmath>

namespace flying::core_sim {

struct Vector3d {
  double x{};
  double y{};
  double z{};

  [[nodiscard]] constexpr Vector3d operator+(Vector3d rhs) const noexcept {
    return {x + rhs.x, y + rhs.y, z + rhs.z};
  }

  [[nodiscard]] constexpr Vector3d operator-(Vector3d rhs) const noexcept {
    return {x - rhs.x, y - rhs.y, z - rhs.z};
  }

  [[nodiscard]] constexpr Vector3d operator*(double scalar) const noexcept {
    return {x * scalar, y * scalar, z * scalar};
  }

  [[nodiscard]] constexpr Vector3d operator/(double scalar) const noexcept {
    return {x / scalar, y / scalar, z / scalar};
  }

  constexpr Vector3d& operator+=(Vector3d rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  constexpr Vector3d& operator-=(Vector3d rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }

  constexpr Vector3d& operator*=(double scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
  }
};

[[nodiscard]] constexpr Vector3d operator*(double scalar, Vector3d value) noexcept {
  return value * scalar;
}

[[nodiscard]] constexpr double dot(Vector3d lhs, Vector3d rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] constexpr Vector3d cross(Vector3d lhs, Vector3d rhs) noexcept {
  return {
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x,
  };
}

struct Quaterniond {
  double w{1.0};
  double x{};
  double y{};
  double z{};

  [[nodiscard]] static constexpr Quaterniond identity() noexcept {
    return {};
  }

  [[nodiscard]] constexpr Quaterniond conjugated() const noexcept {
    return {w, -x, -y, -z};
  }

  [[nodiscard]] Quaterniond normalized() const noexcept {
    const double magnitude_squared = w * w + x * x + y * y + z * z;
    if (magnitude_squared <= 0.0 || !std::isfinite(magnitude_squared)) {
      return identity();
    }

    const double inverse_magnitude = 1.0 / std::sqrt(magnitude_squared);
    return {
      w * inverse_magnitude,
      x * inverse_magnitude,
      y * inverse_magnitude,
      z * inverse_magnitude,
    };
  }

  [[nodiscard]] constexpr Quaterniond operator*(Quaterniond rhs) const noexcept {
    return {
      w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
      w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
      w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
      w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
    };
  }

  [[nodiscard]] Vector3d rotate(Vector3d value) const noexcept {
    const Quaterniond vector_quaternion{0.0, value.x, value.y, value.z};
    const Quaterniond rotated = (*this) * vector_quaternion * conjugated();
    return {rotated.x, rotated.y, rotated.z};
  }
};

} // namespace flying::core_sim
