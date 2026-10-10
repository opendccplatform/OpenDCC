// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "usd_clipboard.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#if PXR_VERSION >= 2508
#include <pxr/usd/sdf/usdFileFormat.h>
#include <pxr/usd/sdf/usdcFileFormat.h>
#else
#include <pxr/usd/usd/usdFileFormat.h>
#include <pxr/usd/usd/usdcFileFormat.h>
#endif

#include "opendcc/base/vendor/ghc/filesystem.hpp"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/core/application.h"

PXR_NAMESPACE_USING_DIRECTIVE;

OPENDCC_NAMESPACE_OPEN

UsdClipboard::UsdClipboard()
{
    auto clipboard_path = ghc::filesystem::temp_directory_path();
    clipboard_path.append("OpenDCCClipboard.usd");
    set_clipboard_path(clipboard_path.string());
#if PXR_VERSION >= 2508
    set_clipboard_file_format(SdfUsdcFileFormatTokens->Id.GetText());
#else
    set_clipboard_file_format(UsdUsdcFileFormatTokens->Id.GetText());
#endif

    if (!ghc::filesystem::exists(m_path_to_clipboard))
    {
        SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
        UsdStageRefPtr clipboard_stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);
        SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));
        set_clipboard(clipboard_stage);
    }
}

UsdClipboard::~UsdClipboard()
{
    if (m_clipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = PXR_NS::UsdUtilsStageCache::Get().Find(m_clipboardStageCacheId);
        PXR_NS::UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    if (m_tmpClipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = PXR_NS::UsdUtilsStageCache::Get().Find(m_tmpClipboardStageCacheId);
        PXR_NS::UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }
}

UsdStageWeakPtr UsdClipboard::get_clipboard()
{
    auto layer = SdfLayer::FindOrOpen(m_path_to_clipboard);
    if (!layer)
    {
        return {};
    }

    UsdStageRefPtr clipboard = UsdStage::Open(m_path_to_clipboard);

    if (m_clipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = UsdUtilsStageCache::Get().Find(m_clipboardStageCacheId);
        UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    m_clipboardStageCacheId = UsdUtilsStageCache::Get().Insert(clipboard);

    clipboard->Reload();

    return clipboard;
}

void UsdClipboard::clear_clipboard()
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);
    SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));
    set_clipboard(stage);
}

void UsdClipboard::set_clipboard(const UsdStageWeakPtr& clipboard)
{
    SdfFileFormat::FileFormatArguments args;
#if PXR_VERSION >= 2508
    args[SdfUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#else
    args[UsdUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#endif
    if (!clipboard->GetRootLayer()->Export(m_path_to_clipboard, "OpenDCCСlipboard", args))
    {
        return;
    }

    clipboard->Unload();
}

void UsdClipboard::set_clipboard_path(const std::string& clipboard_path)
{
    m_path_to_clipboard = clipboard_path;
}

void UsdClipboard::set_clipboard_file_format(const std::string& format)
{
    m_clipboard_file_format = format;
}

void UsdClipboard::save_clipboard_data(const UsdStageWeakPtr& stage)
{
    SdfFileFormat::FileFormatArguments args;
#if PXR_VERSION >= 2508
    args[SdfUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#else
    args[UsdUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#endif
    if (!stage->GetRootLayer()->Export(m_path_to_clipboard, "OpenDCCСlipboard", args))
    {
        return;
    }

    stage->Unload();
}

void UsdClipboard::set_clipboard_attribute(const UsdAttribute& attribute)
{
    save_clipboard_data(attribute.GetStage());
}

void UsdClipboard::set_clipboard_stage(const UsdStageWeakPtr& stage)
{
    save_clipboard_data(stage);
}

UsdAttribute UsdClipboard::get_clipboard_attribute()
{
    auto clipboard_stage = get_clipboard();
    SdfPath attribute_path;

    auto custom_data = clipboard_stage->GetRootLayer()->GetCustomLayerData();
    auto data_type = custom_data.find("stored_data_type");
    if (data_type != custom_data.end() && data_type->second == "attribute")
    {
        auto attr_path = custom_data.find("attribute_path");
        attribute_path = SdfPath(attr_path->second.Get<std::string>());
    }

    if (attribute_path.IsEmpty())
    {
        return {};
    }

    return clipboard_stage->GetAttributeAtPath(attribute_path);
}

UsdStageWeakPtr UsdClipboard::get_clipboard_stage()
{
    auto clipboard_stage = get_clipboard();
    SdfPath attribute_path;

    auto custom_data = clipboard_stage->GetRootLayer()->GetCustomLayerData();
    auto data_type = custom_data.find("stored_data_type");
    if (data_type == custom_data.end() || data_type->second == "attribute")
    {
        return {};
    }
    else
    {
        return clipboard_stage;
    }
}

UsdStageWeakPtr UsdClipboard::get_new_clipboard_stage(const std::string& data_type)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);

    if (m_tmpClipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = UsdUtilsStageCache::Get().Find(m_tmpClipboardStageCacheId);
        UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    m_tmpClipboardStageCacheId = UsdUtilsStageCache::Get().Insert(stage);
    VtDictionary custom_data;
    custom_data["stored_data_type"] = data_type;
    layer->SetCustomLayerData(custom_data);

    return stage;
}

UsdAttribute UsdClipboard::get_new_clipboard_attribute(const SdfValueTypeName& type_name)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);

    SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));

    if (m_tmpClipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = UsdUtilsStageCache::Get().Find(m_tmpClipboardStageCacheId);
        UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    m_tmpClipboardStageCacheId = UsdUtilsStageCache::Get().Insert(stage);
    auto clipboard_attribute = stage->GetPrimAtPath(SdfPath("/Clipboard")).CreateAttribute(TfToken("attribute"), type_name);
    VtDictionary custom_data;
    custom_data["stored_data_type"] = "attribute";
    custom_data["attribute_path"] = clipboard_attribute.GetPath().GetString();
    layer->SetCustomLayerData(custom_data);

    return clipboard_attribute;
}

OPENDCC_NAMESPACE_CLOSE
