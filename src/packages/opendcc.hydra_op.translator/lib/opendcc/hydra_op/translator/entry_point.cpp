// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/translator/entry_point.h"
#include "opendcc/hydra_op/translator/set_attr.h"
#include "opendcc/hydra_op/translator/usd_in.h"
#include "opendcc/hydra_op/translator/merge.h"
#include "opendcc/hydra_op/translator/usd_in.h"
#include "opendcc/hydra_op/translator/set_attr.h"
#include "opendcc/hydra_op/translator/merge.h"
#include "opendcc/hydra_op/translator/prune.h"
#include "opendcc/hydra_op/translator/isolate.h"
#include "opendcc/hydra_op/translator/material_assign.h"
#include "opendcc/hydra_op/translator/translate.h"
#include "opendcc/hydra_op/schema/tokens.h"
#include "opendcc/hydra_op/schema/usdIn.h"
#include "opendcc/hydra_op/schema/merge.h"
#include "opendcc/hydra_op/schema/setAttribute.h"
#include "opendcc/hydra_op/schema/translateAPI.h"
#include "opendcc/hydra_op/translator/registry.h"
#include <pxr/imaging/hdsi/primTypeAndPathPruningSceneIndex.h>
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

void HydraOpTranslatorEntryPoint::initialize(const Package& package)
{
    auto& registry = HydraOpTranslatorRegistry::instance();
    registry.register_node(UsdHydraOpTokens->HydraOpUsdIn, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto usd_in = UsdHydraOpUsdIn(prim))
        {
            return std::make_unique<UsdInTranslator>();
        }
        return nullptr;
    });
    registry.register_node(UsdHydraOpTokens->HydraOpSetAttribute, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto usd_in = UsdHydraOpSetAttribute(prim))
        {
            return std::make_unique<SetAttrTranslator>();
        }
        return nullptr;
    });

    registry.register_node(UsdHydraOpTokens->HydraOpMerge, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto usd_in = UsdHydraOpMerge(prim))
        {
            return std::make_unique<MergeTranslator>();
        }
        return nullptr;
    });

    registry.register_node(UsdHydraOpTokens->HydraOpTranslateAPI, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto translate = UsdHydraOpTranslateAPI(prim))
        {
            return std::make_unique<TranslateAPITranslator>();
        }
        return nullptr;
    });

    registry.register_node(UsdHydraOpTokens->HydraOpPrune, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto prune = UsdHydraOpPrune(prim))
        {
            return std::make_unique<PruneTranslator>();
        }
        return nullptr;
    });
    registry.register_node(UsdHydraOpTokens->HydraOpIsolate, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto prune = UsdHydraOpIsolate(prim))
        {
            return std::make_unique<IsolateTranslator>();
        }
        return nullptr;
    });
    registry.register_node(UsdHydraOpTokens->HydraOpMaterialAssign, [](const UsdPrim& prim) -> std::unique_ptr<HydraOpNodeTranslator> {
        if (auto prune = UsdHydraOpMaterialAssign(prim))
        {
            return std::make_unique<MaterialAssignTranslator>();
        }
        return nullptr;
    });
}

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(HydraOpTranslatorEntryPoint);

OPENDCC_NAMESPACE_CLOSE
