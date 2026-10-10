// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"
#include "opendcc/anim_engine/curve/curve.h"
#include "opendcc/anim_engine/core/anim_engine_curve.h"
#include "opendcc/anim_engine/core/engine.h"

#define ANIM_TEST(expression) DOCTEST_REQUIRE(expression)

#define ANIM_TEST_FLOAT(var0, var1) DOCTEST_REQUIRE((double)var0 == doctest::Approx((double)var1).epsilon(0.01))

OPENDCC_NAMESPACE_USING

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

DOCTEST_TEST_SUITE("AnimEngine")
{
    DOCTEST_TEST_CASE("animation_curve_test1")
    {
        AnimCurve curve;
        adsk::Keyframe k0;
        k0.time = 0;
        k0.value = 1;
        k0.tanIn.type = adsk::TangentType::Linear;
        k0.tanIn.x = 1;
        k0.tanIn.y = 0;
        k0.tanOut.type = adsk::TangentType::Linear;
        k0.tanOut.x = 1;
        k0.tanOut.y = 0;
        adsk::Keyframe k1 = k0;
        k1.time = 1;
        curve.add_key(k0);
        curve.add_key(k1);
        ANIM_TEST(curve.evaluate(0) == 1);
        ANIM_TEST(curve.evaluate(0.5) == 1);
        ANIM_TEST(curve.evaluate(0.9) == 1);
        ANIM_TEST(curve.evaluate(1) == 1);
    }

    DOCTEST_TEST_CASE("animation_curve_test_keyframes")
    {
        AnimCurve curve;
        curve.add_key(0, 1);
        curve.add_key(1, 1);
        curve.add_key(2, 1);
        curve.add_key(3, 1);
        curve.add_key(4, 1);
        adsk::Keyframe k;

        ANIM_TEST(curve.keyframe(-1, k));
        ANIM_TEST(k.time == 0);

        ANIM_TEST(curve.keyframe(0, k));
        ANIM_TEST(k.time == 0);

        ANIM_TEST(curve.keyframe(0.1, k));
        ANIM_TEST(k.time == 1);

        ANIM_TEST(curve.keyframe(0.9, k));
        ANIM_TEST(k.time == 1);

        ANIM_TEST(curve.keyframe(1.9, k));
        ANIM_TEST(k.time == 2);

        ANIM_TEST(curve.keyframe(2, k));
        ANIM_TEST(k.time == 2);

        ANIM_TEST(curve.keyframe(2.1, k));
        ANIM_TEST(k.time == 3);

        ANIM_TEST(curve.keyframe(4, k));
        ANIM_TEST(k.time == 4);

        ANIM_TEST(curve.keyframe(4.1, k));
        ANIM_TEST(k.time == 4);

        ANIM_TEST(curve.keyframe(100, k));
        ANIM_TEST(k.time == 4);
    }

    DOCTEST_TEST_CASE("apply_euler_filter_test")
    {
        using namespace OPENDCC_NAMESPACE;

        auto curve = std::make_shared<AnimEngineCurve>(PXR_NS::UsdAttribute(), 0);
        adsk::Keyframe k0;
        k0.time = 0;
        k0.value = 0;
        k0.tanIn.type = adsk::TangentType::Linear;
        k0.tanIn.x = 1;
        k0.tanIn.y = 0;
        k0.tanOut.type = adsk::TangentType::Linear;
        k0.tanOut.x = 1;
        k0.tanOut.y = 0;
        adsk::Keyframe k1 = k0;
        k1.time = 1;
        k1.value = 360;
        curve->add_key(k0);
        curve->add_key(k1);

        AnimEngine::CurveIdToKeyframesMap start_keyframes_list;
        AnimEngine::CurveIdToKeyframesMap end_keyframes_list;
        auto curve_id = AnimEngine::CurveId();
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);

        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 0);

        curve->at(1).value = 361;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 1);

        curve->at(1).value = 359;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, -1);

        curve->at(1).value = -360;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 0);

        curve->at(1).value = -361;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, -1);

        curve->at(1).value = -359;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 1);

        curve->at(1).value = -720;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 0);

        curve->at(1).value = 720;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 0);

        curve->at(1).value = 120;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 0);

        curve->at(1).value = 181;
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);
        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST(end_keyframes_list[curve_id][0].value == -179);
    }

    DOCTEST_TEST_CASE("apply_euler_filter_test1")
    {
        using namespace OPENDCC_NAMESPACE;

        auto curve = std::make_shared<AnimEngineCurve>(PXR_NS::UsdAttribute(), 0);
        adsk::Keyframe k0;
        k0.time = 0;
        k0.value = 0;
        k0.tanIn.type = adsk::TangentType::Linear;
        k0.tanIn.x = 1;
        k0.tanIn.y = 0;
        k0.tanOut.type = adsk::TangentType::Linear;
        k0.tanOut.x = 1;
        k0.tanOut.y = 0;
        curve->add_key(k0);
        k0.value = 500;
        curve->add_key(k0);
        k0.value = 1000;
        curve->add_key(k0);
        k0.value = 3000;
        curve->add_key(k0);
        k0.value = 0;
        curve->add_key(k0);

        AnimEngine::CurveIdToKeyframesMap start_keyframes_list;
        AnimEngine::CurveIdToKeyframesMap end_keyframes_list;
        auto curve_id = AnimEngine::CurveId();
        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);

        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST(end_keyframes_list[curve_id].size() == 3);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, 140);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][1].value, 280);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][2].value, 120);

        curve->at(1).value = -curve->at(1).value;
        curve->at(2).value = -curve->at(2).value;
        curve->at(3).value = -curve->at(3).value;
        curve->at(4).value = -curve->at(4).value;

        AnimEngine::apply_euler_filter(curve_id, curve, start_keyframes_list, end_keyframes_list);

        ANIM_TEST(end_keyframes_list.size() == 1);
        ANIM_TEST(end_keyframes_list[curve_id].size() == 3);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][0].value, -140);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][1].value, -280);
        ANIM_TEST_FLOAT(end_keyframes_list[curve_id][2].value, -120);
    }
}
