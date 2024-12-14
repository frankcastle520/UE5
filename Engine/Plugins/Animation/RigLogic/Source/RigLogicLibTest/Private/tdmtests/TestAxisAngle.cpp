// Copyright Epic Games, Inc. All Rights Reserved.

#include "tdmtests/Defs.h"
#include "tdmtests/Helpers.h"

#include "tdm/TDM.h"

TEST(TestAxisAngle, ConvertXYZ2AxisAngleAndRotate) {
    using namespace tdm::ang_literals;
    auto m = tdm::mat4<float>::identity();
    tdm::axis_angle<float> axisAngle{tdm::frad{45.0_fdeg}, tdm::frad{0.0_fdeg}, tdm::frad{30.0_fdeg}};
    m = tdm::rotate<float>(m, axisAngle.axis, axisAngle.angle);
    tdm::mat4<float> expected{0.866025f, 0.5f, 0.0f, 0.0f,
                              -0.353553f, 0.612372f, 0.707107f, 0.0f,
                              0.353553f, -0.612372f, 0.707107f, 0.0f,
                              0.0f, 0.0f, 0.0f, 1.0f};
    ASSERT_MAT_NEAR(m, expected, 0.0001f);
}

TEST(TestAxisAngle, ConvertXYZVector2AxisAngleAndRotate) {
    using namespace tdm::ang_literals;
    auto m = tdm::mat4<float>::identity();
    tdm::axis_angle<float> axisAngle{tdm::frad{45.0_fdeg}, tdm::frad{0.0_fdeg}, tdm::frad{30.0_fdeg}};
    m = tdm::rotate<float>(m, axisAngle.axis, axisAngle.angle);
    tdm::mat4<float> expected{0.866025f, 0.5f, 0.0f, 0.0f,
                              -0.353553f, 0.612372f, 0.707107f, 0.0f,
                              0.353553f, -0.612372f, 0.707107f, 0.0f,
                              0.0f, 0.0f, 0.0f, 1.0f};
    ASSERT_MAT_NEAR(m, expected, 0.0001f);
}
