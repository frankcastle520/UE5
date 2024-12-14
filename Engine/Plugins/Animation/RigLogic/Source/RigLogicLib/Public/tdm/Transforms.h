// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "tdm/Computations.h"
#include "tdm/Types.h"

namespace tdm {

inline constexpr double pi() {
    return 3.14159265358979323846;
}

template<typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type degrees(T radians) {
    static constexpr const auto c = static_cast<T>(180.0 / pi());
    return c * radians;
}

template<typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type radians(T degrees) {
    static constexpr const auto c = static_cast<T>(pi() / 180.0);
    return c * degrees;
}

template<typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type fastasin(T value) {
    constexpr float FASTASIN_HALF_PI = 1.5707963050f;
    // Clamp input to [-1,1].
    const bool nonnegative = (value >= 0.0f);
    value = std::abs(value);
    float omx = 1.0f - value;
    if (omx < 0.0f) {
        omx = 0.0f;
    }
    const float root = std::sqrt(omx);
    // 7-degree minimax approximation
    float result =
        ((((((-0.0012624911f
              * value + 0.0066700901f)
             * value - 0.0170881256f)
            * value + 0.0308918810f)
           * value - 0.0501743046f)
          * value + 0.0889789874f)
         * value - 0.2145988016f)
        * value + FASTASIN_HALF_PI;

    result *= root;  // acos(|x|)
    // acos(x) = pi - acos(-x) when x < 0, asin(x) = pi/2 - acos(x)
    return (nonnegative ? FASTASIN_HALF_PI - result : result - FASTASIN_HALF_PI);
}

namespace affine {

template<dim_t L, typename T>
inline mat<L, L, T> scale(const vec<L, T>& factors) {
    return mat<L, L, T>::diagonal(factors);
}

template<dim_t L, typename T>
inline mat<L, L, T> scale(const mat<L, L, T>& m, const vec<L, T>& factors) {
    return m * scale(factors);
}

template<dim_t L, typename T>
inline mat<L, L, T> scale(T factor) {
    return scale(vec<L, T>{factor});
}

template<dim_t L, typename T>
inline mat<L, L, T> scale(const mat<L, L, T>& m, T factor) {
    return scale(m, vec<L, T>{factor});
}

}  // namespace affine

inline namespace projective {

template<typename T>
inline mat4<T> rotate(const vec3<T>& axis, rad<T> angle, handedness h = handedness::right) {
    const rad<T> c{std::cos(angle.value)};
    const rad<T> s{std::sin(angle.value) * static_cast<T>(h)};
    const rad<T> one_minus_c = rad<T>{static_cast<T>(1)} - c;
    const vec3<T> n = normalize(axis);
    return mat4<T>{n[0] * n[0] * one_minus_c.value + c.value,
                   n[1] * n[0] * one_minus_c.value - n[2] * s.value,
                   n[2] * n[0] * one_minus_c.value + n[1] * s.value,
                   static_cast<T>(0),
                   n[0] * n[1] * one_minus_c.value + n[2] * s.value,
                   n[1] * n[1] * one_minus_c.value + c.value,
                   n[2] * n[1] * one_minus_c.value - n[0] * s.value,
                   static_cast<T>(0),
                   n[0] * n[2] * one_minus_c.value - n[1] * s.value,
                   n[1] * n[2] * one_minus_c.value + n[0] * s.value,
                   n[2] * n[2] * one_minus_c.value + c.value,
                   static_cast<T>(0),
                   static_cast<T>(0),
                   static_cast<T>(0),
                   static_cast<T>(0),
                   static_cast<T>(1)};
}

template<typename T>
inline mat4<T> rotate(const mat4<T>& m, const vec3<T>& axis, rad<T> angle, handedness h = handedness::right) {
    return m * rotate(axis, angle, h);
}

template<typename T>
inline mat4<T> rotate(rad<T> x, rad<T> y, rad<T> z, handedness h = handedness::right) {
    const rad<T> sx{std::sin(x.value) * static_cast<T>(h)};
    const rad<T> sy{std::sin(y.value) * static_cast<T>(h)};
    const rad<T> sz{std::sin(z.value) * static_cast<T>(h)};
    const rad<T> cx{std::cos(x.value)};
    const rad<T> cy{std::cos(y.value)};
    const rad<T> cz{std::cos(z.value)};
    auto rx = mat4<T>::identity();
    rx(1, 1) = cx.value;
    rx(1, 2) = sx.value;
    rx(2, 1) = -sx.value;
    rx(2, 2) = cx.value;
    auto ry = mat4<T>::identity();
    ry(0, 0) = cy.value;
    ry(0, 2) = -sy.value;
    ry(2, 0) = sy.value;
    ry(2, 2) = cy.value;
    auto rz = mat4<T>::identity();
    rz(0, 0) = cz.value;
    rz(0, 1) = sz.value;
    rz(1, 0) = -sz.value;
    rz(1, 1) = cz.value;
    return rx * ry * rz;
}

template<typename T>
inline mat4<T> rotate(const mat4<T>& m, rad<T> x, rad<T> y, rad<T> z, handedness h = handedness::right) {
    return m * rotate(x, y, z, h);
}

template<typename T>
inline mat4<T> rotate(const rad3<T>& rotation, handedness h = handedness::right) {
    return rotate(rotation[0], rotation[1], rotation[2], h);
}

template<typename T>
inline mat4<T> rotate(const mat4<T>& m, const rad3<T>& rotation, handedness h = handedness::right) {
    return m * rotate(rotation[0], rotation[1], rotation[2], h);
}

template<dim_t L, typename T>
inline mat<L + 1, L + 1, T> scale(const vec<L, T>& factors) {
    vec<L + 1, T> diagonal{static_cast<T>(1)};
    factors.apply([&diagonal](const T& value, dim_t i) {
                diagonal[i] = value;
            });
    return mat<L + 1, L + 1, T>::diagonal(diagonal);
}

template<dim_t L, typename T>
inline mat<L + 1, L + 1, T> scale(const mat<L + 1, L + 1, T>& m, const vec<L, T>& factors) {
    return m * scale(factors);
}

template<dim_t L, typename T>
inline mat<L + 1, L + 1, T> scale(T factor) {
    return scale(vec<L, T>{factor});
}

template<dim_t L, typename T>
inline mat<L, L, T> scale(const mat<L, L, T>& m, T factor) {
    return scale(m, vec<L - 1, T>{factor});
}

template<dim_t L, typename T>
inline mat<L + 1, L + 1, T> translate(const vec<L, T>& position) {
    auto m = mat<L + 1, L + 1, T>::identity();
    position.apply([&m](const T& value, dim_t i) {
                m(L, i) = value;
            });
    return m;
}

template<dim_t L, typename T>
inline mat<L + 1, L + 1, T> translate(const mat<L + 1, L + 1, T>& m, const vec<L, T>& position) {
    return m * translate(position);
}

}  // namespace projective

}  // namespace tdm
