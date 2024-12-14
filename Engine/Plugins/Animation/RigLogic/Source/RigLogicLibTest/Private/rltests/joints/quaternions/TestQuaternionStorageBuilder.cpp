// Copyright Epic Games, Inc. All Rights Reserved.

#include "rltests/Defs.h"
#include "rltests/StorageValueType.h"
#include "rltests/joints/Helpers.h"
#include "rltests/joints/quaternions/QuaternionFixtures.h"

#include "riglogic/TypeDefs.h"
#include "riglogic/joints/cpu/quaternions/QuaternionJointsBuilder.h"
#include "riglogic/system/simd/Detect.h"

#include <tuple>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4324)
#endif

namespace rl4 {

template<typename T>
struct QuaternionJointsEvaluator<T>::Accessor {

    static void assertRawDataEqual(const QuaternionJointsEvaluator<T>& result, std::size_t rotationSelectorIndex) {
        ASSERT_EQ(result.jointGroups.size(), rltests::qs::unoptimized::subMatrices.size());
        const auto& values = rltests::qs::optimized::Values<T>::get();
        const auto& inputIndices = rltests::qs::optimized::inputIndices;
        const auto& outputIndices = rltests::qs::optimized::outputIndices[rotationSelectorIndex];
        const auto& lods = rltests::qs::optimized::lodRegions;
        for (std::size_t jgi = {}; jgi < result.jointGroups.size(); ++jgi) {
            const auto& jointGroup = result.jointGroups[jgi];
            ASSERT_ELEMENTS_NEAR(jointGroup.values, values[jgi], values[jgi].size(), 0.0002f);
            ASSERT_ELEMENTS_EQ(jointGroup.inputIndices, inputIndices[jgi], inputIndices[jgi].size());
            ASSERT_ELEMENTS_EQ(jointGroup.outputIndices, outputIndices[jgi], outputIndices[jgi].size());
            ASSERT_EQ(jointGroup.lods.size(), lods[jgi].size());
            for (std::size_t lod = {}; lod < lods[jgi].size(); ++lod) {
                const auto& resultLOD = jointGroup.lods[lod];
                const auto& expectedLOD = lods[jgi][lod];
                ASSERT_EQ(resultLOD.size, expectedLOD.size);
                ASSERT_EQ(resultLOD.sizePaddedToLastFullBlock, expectedLOD.sizePaddedToLastFullBlock);
                ASSERT_EQ(resultLOD.sizePaddedToSecondLastFullBlock, expectedLOD.sizePaddedToSecondLastFullBlock);
            }
        }
    }

};

}  // namespace rl4

namespace {

template<typename TTestTypes>
class QuaternionJointStorageBuilderTest : public ::testing::Test {
    protected:
        template<typename TestTypes = TTestTypes>
        typename std::enable_if<std::tuple_size<TestTypes>::value != 0ul, void>::type buildStorage() {
            using T = typename std::tuple_element<0, TTestTypes>::type;
            using TF256 = typename std::tuple_element<1, TTestTypes>::type;
            using TF128 = typename std::tuple_element<2, TTestTypes>::type;
            using TRotationAdapter = typename std::tuple_element<3, TTestTypes>::type;
            using QJEvaluator = rl4::QuaternionJointsEvaluator<T>;

            rl4::Configuration config{};
            config.rotationType = RotationOutputTypeSelector<TRotationAdapter>::rotation();
            rl4::QuaternionJointsBuilder<T, TF256, TF128> builder(config, &memRes);

            rl4::JointBehaviorFilter filter{&reader, &memRes};
            filter.include(dna::RotationRepresentation::Quaternion);

            builder.computeStorageRequirements(filter);
            builder.allocateStorage(filter);
            builder.fillStorage(filter);
            auto joints = builder.build();

            const auto rotationSelectorIndex = RotationOutputTypeSelector<TRotationAdapter>::value();
            rl4::QuaternionJointsEvaluator<T>::Accessor::assertRawDataEqual(*static_cast<QJEvaluator*>(joints.get()),
                                                                            rotationSelectorIndex);
        }

        template<typename TestTypes = TTestTypes>
        typename std::enable_if<std::tuple_size<TestTypes>::value == 0ul, void>::type buildStorage() {
        }

    protected:
        pma::AlignedMemoryResource memRes;
        rltests::qs::QuaternionReader reader;

};

}  // namespace

#if defined(RL_BUILD_WITH_AVX) && defined(RL_BUILD_WITH_SSE)
    using StorageValueTypeList = ::testing::Types<
    #ifdef RL_USE_HALF_FLOATS
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >,
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >
    #else
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >,
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                                 tdm::rot_seq::xyz> >
    #endif  // RL_USE_HALF_FLOATS
        >;
#elif defined(RL_BUILD_WITH_AVX)
    using StorageValueTypeList = ::testing::Types<
    #ifdef RL_USE_HALF_FLOATS
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >
    #else
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::avx::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                                 tdm::rot_seq::xyz> >
    #endif  // RL_USE_HALF_FLOATS
        >;
#elif defined(RL_BUILD_WITH_SSE)
    using StorageValueTypeList = ::testing::Types<
    #ifdef RL_USE_HALF_FLOATS
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >
    #else
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::sse::F256, trimd::sse::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                           tdm::rot_seq::xyz> >,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                                 tdm::rot_seq::xyz> >
    #endif  // RL_USE_HALF_FLOATS
        >;
#else
    #ifndef RL_USE_HALF_FLOATS
        using StorageValueTypeList = ::testing::Types<
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::PassthroughAdapter>,
            std::tuple<StorageValueType, trimd::scalar::F256, trimd::scalar::F128, rl4::QuaternionsToEulerAngles<tdm::frad,
                                                                                                                 tdm::rot_seq::xyz> >
            >;
    #else
        using StorageValueTypeList = ::testing::Types<std::tuple<> >;
    #endif  // RL_USE_HALF_FLOATS
#endif

TYPED_TEST_SUITE(QuaternionJointStorageBuilderTest, StorageValueTypeList, );

TYPED_TEST(QuaternionJointStorageBuilderTest, LayoutOptimization) {
    this->buildStorage();
}

#ifdef _MSC_VER
    #pragma warning(pop)
#endif
