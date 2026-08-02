#pragma once

#include <cmath>

namespace flying::geo_terrain {

inline constexpr double kPi = 3.141592653589793238462643383279502884;

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

  [[nodiscard]] constexpr Vector3d operator-() const noexcept {
    return {-x, -y, -z};
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

[[nodiscard]] inline double norm(Vector3d value) noexcept {
  return std::sqrt(dot(value, value));
}

struct Matrix3d {
  double m00{};
  double m01{};
  double m02{};
  double m10{};
  double m11{};
  double m12{};
  double m20{};
  double m21{};
  double m22{};

  [[nodiscard]] static constexpr Matrix3d identity() noexcept {
    return {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
    };
  }

  [[nodiscard]] constexpr Vector3d operator*(Vector3d rhs) const noexcept {
    return {
      m00 * rhs.x + m01 * rhs.y + m02 * rhs.z,
      m10 * rhs.x + m11 * rhs.y + m12 * rhs.z,
      m20 * rhs.x + m21 * rhs.y + m22 * rhs.z,
    };
  }

  [[nodiscard]] constexpr Matrix3d transposed() const noexcept {
    return {
      m00, m10, m20,
      m01, m11, m21,
      m02, m12, m22,
    };
  }
};

} // namespace flying::geo_terrain
