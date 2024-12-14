// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "tdm/Types.h"
#include "tdm/Ang.h"
#include "tdm/AxisAngle.h"
#include "tdm/Transforms.h"

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cmath>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace tdm {

namespace impl {

template<typename T, rot_seq Order>
struct Euler2Quat;

template<typename T>
struct Euler2Quat<T, rot_seq::xyz> {

    quat<T> operator()(const rad3<T>& rot) {
        rad3<T> h{rot* static_cast<T>(0.5)};
        rad3<T> c = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::cos(angle.value)};
                });
        rad3<T> s = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::sin(angle.value)};
                });

        const auto sx = s[0].value;
        const auto sy = s[1].value;
        const auto sz = s[2].value;

        const auto cx = c[0].value;
        const auto cy = c[1].value;
        const auto cz = c[2].value;

        const auto x = sx * cy * cz + cx * sy * sz;
        const auto y = cx * sy * cz - sx * cy * sz;
        const auto z = cx * cy * sz + sx * sy * cz;
        const auto w = cx * cy * cz - sx * sy * sz;

        return quat<T>{x, y, z, w};
    }

};

template<typename T>
struct Euler2Quat<T, rot_seq::xzy> {

    quat<T> operator()(const rad3<T>& rot) {
        rad3<T> h{rot* static_cast<T>(0.5)};
        rad3<T> c = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::cos(angle.value)};
                });
        rad3<T> s = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::sin(angle.value)};
                });

        const auto sx = s[0].value;
        const auto sy = s[1].value;
        const auto sz = s[2].value;

        const auto cx = c[0].value;
        const auto cy = c[1].value;
        const auto cz = c[2].value;

        const auto x = sx * cy * cz - cx * sy * sz;
        const auto y = cx * sy * cz - sx * cy * sz;
        const auto z = cx * cy * sz + sx * sy * cz;
        const auto w = cx * cy * cz + sx * sy * sz;

        return quat<T>{x, y, z, w};
    }

};

template<typename T>
struct Euler2Quat<T, rot_seq::yxz> {

    quat<T> operator()(const rad3<T>& rot) {
        rad3<T> h{rot* static_cast<T>(0.5)};
        rad3<T> c = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::cos(angle.value)};
                });
        rad3<T> s = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::sin(angle.value)};
                });

        const auto sx = s[0].value;
        const auto sy = s[1].value;
        const auto sz = s[2].value;

        const auto cx = c[0].value;
        const auto cy = c[1].value;
        const auto cz = c[2].value;

        const auto x = sx * cy * cz + cx * sy * sz;
        const auto y = cx * sy * cz - sx * cy * sz;
        const auto z = cx * cy * sz - sx * sy * cz;
        const auto w = cx * cy * cz + sx * sy * sz;

        return quat<T>{x, y, z, w};
    }

};

template<typename T>
struct Euler2Quat<T, rot_seq::yzx> {

    quat<T> operator()(const rad3<T>& rot) {
        rad3<T> h{rot* static_cast<T>(0.5)};
        rad3<T> c = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::cos(angle.value)};
                });
        rad3<T> s = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::sin(angle.value)};
                });

        const auto sx = s[0].value;
        const auto sy = s[1].value;
        const auto sz = s[2].value;

        const auto cx = c[0].value;
        const auto cy = c[1].value;
        const auto cz = c[2].value;

        const auto x = sx * cy * cz + cx * sy * sz;
        const auto y = cx * sy * cz + sx * cy * sz;
        const auto z = cx * cy * sz - sx * sy * cz;
        const auto w = cx * cy * cz - sx * sy * sz;

        return quat<T>{x, y, z, w};
    }

};

template<typename T>
struct Euler2Quat<T, rot_seq::zxy> {

    quat<T> operator()(const rad3<T>& rot) {
        rad3<T> h{rot* static_cast<T>(0.5)};
        rad3<T> c = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::cos(angle.value)};
                });
        rad3<T> s = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::sin(angle.value)};
                });

        const auto sx = s[0].value;
        const auto sy = s[1].value;
        const auto sz = s[2].value;

        const auto cx = c[0].value;
        const auto cy = c[1].value;
        const auto cz = c[2].value;

        const auto x = sx * cy * cz - cx * sy * sz;
        const auto y = cx * sy * cz + sx * cy * sz;
        const auto z = cx * cy * sz + sx * sy * cz;
        const auto w = cx * cy * cz - sx * sy * sz;

        return quat<T>{x, y, z, w};
    }

};

template<typename T>
struct Euler2Quat<T, rot_seq::zyx> {

    quat<T> operator()(const rad3<T>& rot) {
        rad3<T> h{rot* static_cast<T>(0.5)};
        rad3<T> c = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::cos(angle.value)};
                });
        rad3<T> s = applied(h, [](rad<T>& angle, dim_t  /*unused*/) {
                    angle = rad<T>{std::sin(angle.value)};
                });

        const auto sx = s[0].value;
        const auto sy = s[1].value;
        const auto sz = s[2].value;

        const auto cx = c[0].value;
        const auto cy = c[1].value;
        const auto cz = c[2].value;

        const auto x = sx * cy * cz - cx * sy * sz;
        const auto y = cx * sy * cz + sx * cy * sz;
        const auto z = cx * cy * sz - sx * sy * cz;
        const auto w = cx * cy * cz + sx * sy * sz;

        return quat<T>{x, y, z, w};
    }

};

template<typename T, rot_seq Order>
struct Quat2Euler;

template<typename T>
struct Quat2Euler<T, rot_seq::xyz> {

    rad3<T> operator()(const quat<T>& q) {
        const T x2 = q.x + q.x;
        const T y2 = q.y + q.y;
        const T z2 = q.z + q.z;

        const T xx2 = q.x * x2;
        const T xy2 = q.x * y2;
        const T xz2 = q.x * z2;

        const T yy2 = q.y * y2;
        const T yz2 = q.y * z2;

        const T zz2 = q.z * z2;

        const T wx2 = q.w * x2;
        const T wy2 = q.w * y2;
        const T wz2 = q.w * z2;

        rad3<T> angles;
        const T sy = xz2 + wy2;
        angles[1] = rad<T>(fastasin(sy));
        if (std::abs(sy) < static_cast<T>(0.99999999999)) {
            angles[0] = rad<T>(std::atan2(wx2 - yz2, static_cast<T>(1.0) - (xx2 + yy2)));
            angles[2] = rad<T>(std::atan2(wz2 - xy2, static_cast<T>(1.0) - (yy2 + zz2)));
        } else {
            angles[0] = rad<T>(std::atan2(yz2 + wx2, static_cast<T>(1.0) - (xx2 + zz2)));
            angles[2] = rad<T>(static_cast<T>(0.0));
        }
        return angles;
    }

};

template<typename T>
struct Quat2Euler<T, rot_seq::xzy> {

    rad3<T> operator()(const quat<T>& q) {
        const T x2 = q.x + q.x;
        const T y2 = q.y + q.y;
        const T z2 = q.z + q.z;

        const T xx2 = q.x * x2;
        const T xy2 = q.x * y2;
        const T xz2 = q.x * z2;

        const T yy2 = q.y * y2;
        const T yz2 = q.y * z2;

        const T zz2 = q.z * z2;

        const T wx2 = q.w * x2;
        const T wy2 = q.w * y2;
        const T wz2 = q.w * z2;

        rad3<T> angles;
        const T sz = xy2 - wz2;
        angles[2] = rad<T>(fastasin(-sz));
        if (std::abs(sz) < static_cast<T>(0.99999999999)) {
            angles[0] = rad<T>(std::atan2(yz2 + wx2, static_cast<T>(1.0) - (xx2 + zz2)));
            angles[1] = rad<T>(std::atan2(xz2 + wy2, static_cast<T>(1.0) - (yy2 + zz2)));
        } else {
            angles[0] = rad<T>(std::atan2(wx2 - yz2, static_cast<T>(1.0) - (xx2 + yy2)));
            angles[1] = rad<T>(static_cast<T>(0.0));
        }
        return angles;
    }

};

template<typename T>
struct Quat2Euler<T, rot_seq::yxz> {

    rad3<T> operator()(const quat<T>& q) {
        const T x2 = q.x + q.x;
        const T y2 = q.y + q.y;
        const T z2 = q.z + q.z;

        const T xx2 = q.x * x2;
        const T xy2 = q.x * y2;
        const T xz2 = q.x * z2;

        const T yy2 = q.y * y2;
        const T yz2 = q.y * z2;

        const T zz2 = q.z * z2;

        const T wx2 = q.w * x2;
        const T wy2 = q.w * y2;
        const T wz2 = q.w * z2;

        rad3<T> angles;
        const T sx = yz2 - wx2;
        angles[0] = rad<T>(fastasin(-sx));
        if (std::abs(sx) < static_cast<T>(0.99999999999)) {
            angles[1] = rad<T>(std::atan2(xz2 + wy2, static_cast<T>(1.0) - (xx2 + yy2)));
            angles[2] = rad<T>(std::atan2(xy2 + wz2, static_cast<T>(1.0) - (xx2 + zz2)));
        } else {
            angles[1] = rad<T>(std::atan2(wy2 - xz2, static_cast<T>(1.0) - (yy2 + zz2)));
            angles[2] = rad<T>(static_cast<T>(0.0));
        }
        return angles;
    }

};

template<typename T>
struct Quat2Euler<T, rot_seq::yzx> {

    rad3<T> operator()(const quat<T>& q) {
        const T x2 = q.x + q.x;
        const T y2 = q.y + q.y;
        const T z2 = q.z + q.z;

        const T xx2 = q.x * x2;
        const T xy2 = q.x * y2;
        const T xz2 = q.x * z2;

        const T yy2 = q.y * y2;
        const T yz2 = q.y * z2;

        const T zz2 = q.z * z2;

        const T wx2 = q.w * x2;
        const T wy2 = q.w * y2;
        const T wz2 = q.w * z2;

        rad3<T> angles;
        const T sz = xy2 + wz2;
        angles[2] = rad<T>(fastasin(sz));
        if (std::abs(sz) < static_cast<T>(0.99999999999)) {
            angles[0] = rad<T>(std::atan2(wx2 - yz2, static_cast<T>(1.0) - (xx2 + zz2)));
            angles[1] = rad<T>(std::atan2(wy2 - xz2, static_cast<T>(1.0) - (yy2 + zz2)));
        } else {
            angles[0] = rad<T>(static_cast<T>(0.0));
            angles[1] = rad<T>(std::atan2(xz2 + wy2, static_cast<T>(1.0) - (xx2 + yy2)));
        }
        return angles;
    }

};

template<typename T>
struct Quat2Euler<T, rot_seq::zxy> {

    rad3<T> operator()(const quat<T>& q) {
        const T x2 = q.x + q.x;
        const T y2 = q.y + q.y;
        const T z2 = q.z + q.z;

        const T xx2 = q.x * x2;
        const T xy2 = q.x * y2;
        const T xz2 = q.x * z2;

        const T yy2 = q.y * y2;
        const T yz2 = q.y * z2;

        const T zz2 = q.z * z2;

        const T wx2 = q.w * x2;
        const T wy2 = q.w * y2;
        const T wz2 = q.w * z2;

        rad3<T> angles;
        const T sx = yz2 + wx2;
        angles[0] = rad<T>(fastasin(sx));
        if (std::abs(sx) < static_cast<T>(0.99999999999)) {
            angles[1] = rad<T>(std::atan2(wy2 - xz2, static_cast<T>(1.0) - (xx2 + yy2)));
            angles[2] = rad<T>(std::atan2(wz2 - xy2, static_cast<T>(1.0) - (xx2 + zz2)));
        } else {
            angles[1] = rad<T>(static_cast<T>(0.0));
            angles[2] = rad<T>(std::atan2(xy2 + wz2, static_cast<T>(1.0) - (yy2 + zz2)));
        }
        return angles;
    }

};

template<typename T>
struct Quat2Euler<T, rot_seq::zyx> {

    rad3<T> operator()(const quat<T>& q) {
        const T x2 = q.x + q.x;
        const T y2 = q.y + q.y;
        const T z2 = q.z + q.z;

        const T xx2 = q.x * x2;
        const T xy2 = q.x * y2;
        const T xz2 = q.x * z2;

        const T yy2 = q.y * y2;
        const T yz2 = q.y * z2;

        const T zz2 = q.z * z2;

        const T wx2 = q.w * x2;
        const T wy2 = q.w * y2;
        const T wz2 = q.w * z2;

        rad3<T> angles;
        const T sy = xz2 - wy2;
        angles[1] = rad<T>(fastasin(-sy));
        if (std::abs(sy) < static_cast<T>(0.99999999999)) {
            angles[0] = rad<T>(std::atan2(yz2 + wx2, static_cast<T>(1.0) - (xx2 + yy2)));
            angles[2] = rad<T>(std::atan2(xy2 + wz2, static_cast<T>(1.0) - (yy2 + zz2)));
        } else {
            angles[0] = rad<T>(static_cast<T>(0.0));
            angles[2] = rad<T>(std::atan2(wz2 - xy2, static_cast<T>(1.0) - (xx2 + zz2)));
        }
        return angles;
    }

};

template<typename T>
quat<T> euler2quat(const rad3<T>& rot, rot_seq order) {
    switch (order) {
        case rot_seq::xyz:
            return Euler2Quat<T, rot_seq::xyz>()(rot);
        case rot_seq::xzy:
            return Euler2Quat<T, rot_seq::xzy>()(rot);
        case rot_seq::yxz:
            return Euler2Quat<T, rot_seq::yxz>()(rot);
        case rot_seq::yzx:
            return Euler2Quat<T, rot_seq::yzx>()(rot);
        case rot_seq::zxy:
            return Euler2Quat<T, rot_seq::zxy>()(rot);
        case rot_seq::zyx:
            return Euler2Quat<T, rot_seq::zyx>()(rot);
    }
    return {};
}

template<typename T>
rad3<T> quat2euler(const quat<T>& q, rot_seq order) {
    switch (order) {
        case rot_seq::xyz:
            return Quat2Euler<T, rot_seq::xyz>()(q);
        case rot_seq::xzy:
            return Quat2Euler<T, rot_seq::xzy>()(q);
        case rot_seq::yxz:
            return Quat2Euler<T, rot_seq::yxz>()(q);
        case rot_seq::yzx:
            return Quat2Euler<T, rot_seq::yzx>()(q);
        case rot_seq::zxy:
            return Quat2Euler<T, rot_seq::zxy>()(q);
        case rot_seq::zyx:
            return Quat2Euler<T, rot_seq::zyx>()(q);
    }
    return {};
}

}  // namespace impl

template<typename T>
struct quat {
    using value_type = T;

    value_type x;
    value_type y;
    value_type z;
    value_type w;

    quat() : x{}, y{}, z{}, w{static_cast<value_type>(1.0)} {
    }

    quat(value_type x_, value_type y_, value_type z_, value_type w_) : x{x_}, y{y_}, z{z_}, w{w_} {
    }

    template<rot_seq Order>
    static quat<T> fromEuler(const rad3<T>& rot) {
        return impl::Euler2Quat<value_type, Order>()(rot);
    }

    explicit quat(const rad3<value_type>& rot, rot_seq order) : quat{impl::euler2quat(rot, order)} {
    }

    explicit quat(const axis_angle<value_type>& rot) {
        const value_type s = std::sin(rot.angle.value / static_cast<value_type>(2.0));
        const value_type c = std::cos(rot.angle.value / static_cast<value_type>(2.0));
        x = s * rot.axis[0];
        y = s * rot.axis[1];
        z = s * rot.axis[2];
        w = c;
    }

    quat& operator+=(const quat& rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        w += rhs.w;
        return *this;
    }

    quat& operator-=(const quat& rhs) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        w -= rhs.w;
        return *this;
    }

    quat& operator*=(const quat& rhs) {
        const quat lhs{*this};
        x = lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y;
        y = lhs.w * rhs.y + lhs.y * rhs.w + lhs.z * rhs.x - lhs.x * rhs.z;
        z = lhs.w * rhs.z + lhs.z * rhs.w + lhs.x * rhs.y - lhs.y * rhs.x;
        w = lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z;
        return *this;
    }

    quat& operator*=(value_type val) {
        x *= val;
        y *= val;
        z *= val;
        w *= val;
        return *this;
    }

    quat& operator/=(value_type val) {
        x /= val;
        y /= val;
        z /= val;
        w /= val;
        return *this;
    }

    quat operator-() const {
        return quat{-x, -y, -z, -w};
    }

    template<rot_seq Order>
    rad3<value_type> euler() const {
        return impl::Quat2Euler<value_type, Order>()(*this);
    }

    rad3<value_type> euler(rot_seq order) const {
        return impl::quat2euler(*this, order);
    }

    quat& negate() {
        x = -x;
        y = -y;
        z = -z;
        w = -w;
        return *this;
    }

    value_type length2() const {
        return x * x + y * y + z * z + w * w;
    }

    value_type length() const {
        return std::sqrt(length2());
    }

    quat& normalize() {
        const value_type il = static_cast<T>(1.0) / length();
        x *= il;
        y *= il;
        z *= il;
        w *= il;
        return *this;
    }

};

template<typename T>
inline bool operator==(const quat<T>& lhs, const quat<T>& rhs) {
    return (lhs.x == rhs.x) && (lhs.y == rhs.y) && (lhs.z == rhs.z) && (lhs.w == rhs.w);
}

template<typename T>
inline bool operator!=(const quat<T>& lhs, const quat<T>& rhs) {
    return !(lhs == rhs);
}

template<typename T>
inline quat<T> operator+(const quat<T>& lhs, const quat<T>& rhs) {
    return quat<T>(lhs) += rhs;
}

template<typename T>
inline quat<T> operator-(const quat<T>& lhs, const quat<T>& rhs) {
    return quat<T>(lhs) -= rhs;
}

template<typename T>
inline quat<T> operator*(const quat<T>& lhs, const quat<T>& rhs) {
    return quat<T>(lhs) *= rhs;
}

template<typename T>
inline quat<T> operator*(const quat<T>& lhs, T rhs) {
    return quat<T>(lhs) *= rhs;
}

template<typename T>
inline quat<T> operator*(T lhs, const quat<T>& rhs) {
    return rhs * lhs;
}

template<typename T>
inline quat<T> operator/(const quat<T>& lhs, T rhs) {
    return quat<T>(lhs) /= rhs;
}

template<typename T>
inline quat<T> operator/(T lhs, const quat<T>& rhs) {
    return rhs / lhs;
}

}  // namespace tdm
