// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "tdm/Types.h"
#include "tdm/Ang.h"
#include "tdm/Mat.h"
#include "tdm/Computations.h"
#include "tdm/Transforms.h"

namespace tdm {

template<typename T>
struct axis_angle {
    vec3<T> axis;
    rad<T> angle;

    axis_angle(vec3<T> axis_, rad<T> angle_) : axis{axis_}, angle{angle_} {
    }

    axis_angle(rad<T> x, rad<T> y, rad<T> z, handedness h = handedness::right) {
        const mat4<T> m = rotate(x, y, z, h);
        angle = rad<T>{std::acos((trace(m.template submat<3, 3>(0, 0)) - static_cast<T>(1.0)) / static_cast<T>(2.0))};
        const T factor = static_cast<T>(1.0) / (static_cast<T>(2.0) * std::sin(angle.value));
        axis[0] = factor * (m(2, 1) - m(1, 2));
        axis[1] = factor * (m(0, 2) - m(2, 0));
        axis[2] = factor * (m(1, 0) - m(0, 1));
    }

    axis_angle(const rad3<T>& angles, handedness h = handedness::right) : axis_angle{angles[0], angles[1], angles[2], h} {
    }

};


}  // namespace tdm
