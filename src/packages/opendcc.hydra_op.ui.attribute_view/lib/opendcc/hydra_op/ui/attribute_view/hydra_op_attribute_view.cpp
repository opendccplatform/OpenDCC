//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Modifications copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "hydra_op_attribute_view.h"
#include "dataSourceTreeWidget.h"
#include "dataSourceValueTreeView.h"
#include "opendcc/hydra_op/translator/terminal_scene_index.h"

#include "pxr/imaging/hd/filteringSceneIndex.h"
#if PXR_VERSION >= 2408
#include "pxr/imaging/hd/utils.h"
#endif
#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/tf/stringUtils.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <fstream>
#include <iostream>
#include <sstream>
#include <typeinfo>

PXR_NAMESPACE_OPEN_SCOPE

#if PXR_VERSION < 2408
// XXX stevel: low-tech temporary symbol name demangling until we manage these
// via a formal plug-in/type registry
static std::string Hdui_StripNumericPrefix(const std::string &s)
{
    return TfStringTrimLeft(s, "0123456789");
}
#endif

HduiSceneIndexDebuggerWidget::HduiSceneIndexDebuggerWidget(QWidget *parent, const Options &options)
    : QWidget(parent)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *toolbarLayout = new QHBoxLayout;
    mainLayout->addLayout(toolbarLayout);

    _splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(_splitter, 10);

    _dsTreeWidget = new HduiDataSourceTreeWidget;
    _splitter->addWidget(_dsTreeWidget);

    _valueTreeView = new HduiDataSourceValueTreeView;
    _splitter->addWidget(_valueTreeView);

    m_selection_event_hndl = OpenDCC::HydraOpSession::instance().register_event_handler(OpenDCC::HydraOpSession::EventType::SelectionChanged, [this] {
        // Fires on deselect too, and there is no view scene index until a graph is cooked.
        const auto selection = OpenDCC::HydraOpSession::instance().get_selection();
        const auto sceneIndex = OpenDCC::HydraOpSession::instance().get_view_scene_index();
        this->_valueTreeView->SetDataSource(nullptr);
        if (selection.empty() || !sceneIndex)
        {
            this->_dsTreeWidget->SetPrimDataSource(SdfPath::EmptyPath(), nullptr);
            return;
        }

        const auto primPath = selection.begin()->first;
        this->_dsTreeWidget->SetPrimDataSource(primPath, sceneIndex->GetPrim(primPath).dataSource);
    });

    QObject::connect(_dsTreeWidget, &HduiDataSourceTreeWidget::DataSourceSelected,
                     [this](HdDataSourceBaseHandle dataSource) { this->_valueTreeView->SetDataSource(HdSampledDataSource::Cast(dataSource)); });
}

HduiSceneIndexDebuggerWidget::~HduiSceneIndexDebuggerWidget()
{
    if (m_selection_event_hndl)
    {
        OpenDCC::HydraOpSession::instance().unregister_event_handler(OpenDCC::HydraOpSession::EventType::SelectionChanged, m_selection_event_hndl);
    }
}

namespace
{
    class _InputSelectionItem : public QTreeWidgetItem
    {
    public:
        _InputSelectionItem(QTreeWidgetItem *parent)
            : QTreeWidgetItem(parent)
        {
        }

        HdSceneIndexBasePtr sceneIndex;
    };
}

void HduiSceneIndexDebuggerWidget::_AddSceneIndexToTreeMenu(QTreeWidgetItem *parentItem, HdSceneIndexBaseRefPtr sceneIndex, bool includeSelf)
{
    if (!sceneIndex)
    {
        return;
    }

    if (includeSelf)
    {
        _InputSelectionItem *item = new _InputSelectionItem(parentItem);
#if PXR_VERSION >= 2408
        item->setText(0, sceneIndex->GetDisplayName().c_str());
#else
        item->setText(0, Hdui_StripNumericPrefix(typeid(*sceneIndex).name()).c_str());
#endif
        item->sceneIndex = sceneIndex;
        item->treeWidget()->resizeColumnToContents(0);
        parentItem = item;
    }

    if (HdFilteringSceneIndexBaseRefPtr filteringSi = TfDynamic_cast<HdFilteringSceneIndexBaseRefPtr>(sceneIndex))
    {
        // TODO, handling multi-input branching
        std::vector<HdSceneIndexBaseRefPtr> sceneIndices = filteringSi->GetInputScenes();
        if (!sceneIndices.empty())
        {
            parentItem->setExpanded(true);
            for (HdSceneIndexBaseRefPtr childSceneIndex : sceneIndices)
            {
                _AddSceneIndexToTreeMenu(parentItem, childSceneIndex, true);
            }
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
