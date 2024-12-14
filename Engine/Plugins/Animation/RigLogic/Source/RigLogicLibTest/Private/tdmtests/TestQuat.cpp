// Copyright Epic Games, Inc. All Rights Reserved.

#include "tdmtests/Defs.h"
#include "tdmtests/Helpers.h"

#include "tdm/TDM.h"

TEST(QuatTestConstruction, FromEulerAnglesXYZ1) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{30.0_fdeg}, tdm::frad{-45.0_fdeg}, tdm::frad{-15.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::xyz};
    ASSERT_NEAR(q.x, 0.2853201f, 1e-4f);
    ASSERT_NEAR(q.y, -0.3352703f, 1e-4f);
    ASSERT_NEAR(q.z, -0.2146799f, 1e-4f);
    ASSERT_NEAR(q.w, 0.8718364f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesXZY1) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{30.0_fdeg}, tdm::frad{-45.0_fdeg}, tdm::frad{-15.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::xzy};
    ASSERT_NEAR(q.x, 0.1888237f, 1e-4f);
    ASSERT_NEAR(q.y, -0.3352703f, 1e-4f);
    ASSERT_NEAR(q.z, -0.2146799f, 1e-4f);
    ASSERT_NEAR(q.w, 0.8976926f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesYXZ1) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{30.0_fdeg}, tdm::frad{-45.0_fdeg}, tdm::frad{-15.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::yxz};
    ASSERT_NEAR(q.x, 0.2853201f, 1e-4f);
    ASSERT_NEAR(q.y, -0.3352703f, 1e-4f);
    ASSERT_NEAR(q.z, -0.018283f, 1e-4f);
    ASSERT_NEAR(q.w, 0.8976926f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesYZX1) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{30.0_fdeg}, tdm::frad{-45.0_fdeg}, tdm::frad{-15.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::yzx};
    ASSERT_NEAR(q.x, 0.2853201f, 1e-4f);
    ASSERT_NEAR(q.y, -0.3976926f, 1e-4f);
    ASSERT_NEAR(q.z, -0.018283f, 1e-4f);
    ASSERT_NEAR(q.w, 0.8718364f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesZXY1) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{30.0_fdeg}, tdm::frad{-45.0_fdeg}, tdm::frad{-15.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::zxy};
    ASSERT_NEAR(q.x, 0.1888237f, 1e-4f);
    ASSERT_NEAR(q.y, -0.3976926f, 1e-4f);
    ASSERT_NEAR(q.z, -0.2146799f, 1e-4f);
    ASSERT_NEAR(q.w, 0.8718364f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesZYX1) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{30.0_fdeg}, tdm::frad{-45.0_fdeg}, tdm::frad{-15.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::zyx};
    ASSERT_NEAR(q.x, 0.1888237f, 1e-4f);
    ASSERT_NEAR(q.y, -0.3976926f, 1e-4f);
    ASSERT_NEAR(q.z, -0.018283f, 1e-4f);
    ASSERT_NEAR(q.w, 0.8976926f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesXYZ2) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{0.0_fdeg}, tdm::frad{-30.0_fdeg}, tdm::frad{-30.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::xyz};
    ASSERT_NEAR(q.x, 0.0669873f, 1e-4f);
    ASSERT_NEAR(q.y, -0.25f, 1e-4f);
    ASSERT_NEAR(q.z, -0.25f, 1e-4f);
    ASSERT_NEAR(q.w, 0.9330127f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesXZY2) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{0.0_fdeg}, tdm::frad{-30.0_fdeg}, tdm::frad{-30.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::xzy};
    ASSERT_NEAR(q.x, -0.0669873f, 1e-4f);
    ASSERT_NEAR(q.y, -0.25f, 1e-4f);
    ASSERT_NEAR(q.z, -0.25f, 1e-4f);
    ASSERT_NEAR(q.w, 0.9330127f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesYXZ2) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{0.0_fdeg}, tdm::frad{-30.0_fdeg}, tdm::frad{-30.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::yxz};
    ASSERT_NEAR(q.x, 0.0669873f, 1e-4f);
    ASSERT_NEAR(q.y, -0.25f, 1e-4f);
    ASSERT_NEAR(q.z, -0.25f, 1e-4f);
    ASSERT_NEAR(q.w, 0.9330127f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesYZX2) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{0.0_fdeg}, tdm::frad{-30.0_fdeg}, tdm::frad{-30.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::yzx};
    ASSERT_NEAR(q.x, 0.0669873f, 1e-4f);
    ASSERT_NEAR(q.y, -0.25f, 1e-4f);
    ASSERT_NEAR(q.z, -0.25f, 1e-4f);
    ASSERT_NEAR(q.w, 0.9330127f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesZXY2) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{0.0_fdeg}, tdm::frad{-30.0_fdeg}, tdm::frad{-30.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::zxy};
    ASSERT_NEAR(q.x, -0.0669873f, 1e-4f);
    ASSERT_NEAR(q.y, -0.25f, 1e-4f);
    ASSERT_NEAR(q.z, -0.25f, 1e-4f);
    ASSERT_NEAR(q.w, 0.9330127f, 1e-4f);
}

TEST(QuatTestConstruction, FromEulerAnglesZYX2) {
    using namespace tdm::ang_literals;
    const tdm::frad3 euler{tdm::frad{0.0_fdeg}, tdm::frad{-30.0_fdeg}, tdm::frad{-30.0_fdeg}};
    const tdm::fquat q{euler, tdm::rot_seq::zyx};
    ASSERT_NEAR(q.x, -0.06698729f, 1e-4f);
    ASSERT_NEAR(q.y, -0.25f, 1e-4f);
    ASSERT_NEAR(q.z, -0.25f, 1e-4f);
    ASSERT_NEAR(q.w, 0.9330127f, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesXYZ1) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.2853201f, -0.3352703f, -0.2146799f, 0.8718364f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::xyz);
    ASSERT_NEAR(result[0].value, tdm::frad{30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-45.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-15.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesXZY1) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.1888237f, -0.3352703f, -0.2146799f, 0.8976926f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::xzy);
    ASSERT_NEAR(result[0].value, tdm::frad{30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-45.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-15.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesYXZ1) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.2853201f, -0.3352703f, -0.018283f, 0.8976926f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::yxz);
    ASSERT_NEAR(result[0].value, tdm::frad{30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-45.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-15.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesYZX1) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.2853201f, -0.3976926f, -0.018283f, 0.8718364f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::yzx);
    ASSERT_NEAR(result[0].value, tdm::frad{30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-45.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-15.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesZXY1) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.1888237f, -0.3976926f, -0.2146799f, 0.8718364f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::zxy);
    ASSERT_NEAR(result[0].value, tdm::frad{30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-45.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-15.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesZYX1) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.1888237f, -0.3976926f, -0.018283f, 0.8976926f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::zyx);
    ASSERT_NEAR(result[0].value, tdm::frad{30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-45.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-15.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesXYZ2) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.0669873f, -0.25f, -0.25f, 0.9330127f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::xyz);
    ASSERT_NEAR(result[0].value, tdm::frad{0.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesXZY2) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{-0.0669873f, -0.25f, -0.25f, 0.9330127f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::xzy);
    ASSERT_NEAR(result[0].value, tdm::frad{0.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesYXZ2) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.0669873f, -0.25f, -0.25f, 0.9330127f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::yxz);
    ASSERT_NEAR(result[0].value, tdm::frad{0.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesYZX2) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{0.0669873f, -0.25f, -0.25f, 0.9330127f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::yzx);
    ASSERT_NEAR(result[0].value, tdm::frad{0.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesZXY2) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{-0.0669873f, -0.25f, -0.25f, 0.9330127f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::zxy);
    ASSERT_NEAR(result[0].value, tdm::frad{0.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, ToEulerAnglesZYX2) {
    using namespace tdm::ang_literals;
    const tdm::fquat q{-0.06698729f, -0.25f, -0.25f, 0.9330127f};
    const tdm::frad3 result = q.euler(tdm::rot_seq::zyx);
    ASSERT_NEAR(result[0].value, tdm::frad{0.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[1].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
    ASSERT_NEAR(result[2].value, tdm::frad{-30.0_fdeg}.value, 1e-4f);
}

TEST(QuatTestConstruction, FromAxisAngle) {
    using namespace tdm::ang_literals;
    const tdm::axis_angle<float> aa{{0.7319547f, 0.5350516f, 0.4218555f}, 0.6416255_frad};
    const tdm::fquat q{aa};
    ASSERT_NEAR(q.x, 0.230813f, 1e-4f);
    ASSERT_NEAR(q.y, 0.168722f, 1e-4f);
    ASSERT_NEAR(q.z, 0.133027f, 1e-4f);
    ASSERT_NEAR(q.w, 0.948979f, 1e-4f);
}

TEST(QuatTestConstruction, Concatenation) {
    const tdm::fquat q1{0.1888237f, -0.3976926f, -0.018283f, 0.8976926f};
    const tdm::fquat q2{-0.06698729f, -0.24999999f, -0.24999999f, 0.93301270f};
    const tdm::fquat qc = q1 * q2;
    ASSERT_NEAR(qc.x, 0.210893303f, 1e-4f);
    ASSERT_NEAR(qc.y, -0.547044694f, 1e-4f);
    ASSERT_NEAR(qc.z, -0.315327674f, 1e-4f);
    ASSERT_NEAR(qc.w, 0.746213555f, 1e-4f);
}
