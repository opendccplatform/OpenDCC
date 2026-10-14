//
// Copyright 2022 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Modifications copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "sceneIndexDebuggerWidget.h"
#include "opendcc/base/logging/logger.h"
#include "dataSourceTreeWidget.h"
#include "dataSourceValueTreeView.h"
#include "sceneIndexTreeWidget.h"
#include "registeredSceneIndexChooser.h"
#include "sceneIndexObserverLoggingWidget.h"
#include "sceneIndexObserverLoggingTreeView.h"

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

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);
OPENDCC_NAMESPACE_CLOSE

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
    , _goToInputButton(Q_NULLPTR)
    , _goToInputButtonMenu(Q_NULLPTR)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *toolbarLayout = new QHBoxLayout;
    mainLayout->addLayout(toolbarLayout);

    _siChooser = new HduiRegisteredSceneIndexChooser;
    toolbarLayout->addWidget(_siChooser);

    if (options.showInputsButton)
    {
        _goToInputButton = new QPushButton("Inputs");
        _goToInputButton->setEnabled(false);
        _goToInputButtonMenu = new QMenu(this);
        _goToInputButton->setMenu(_goToInputButtonMenu);
    }

    toolbarLayout->addWidget(_goToInputButton);

    _nameLabel = new QLabel;
    toolbarLayout->addWidget(_nameLabel, 10);

    QPushButton *loggerButton = new QPushButton("Show Notice Logger");
    toolbarLayout->addWidget(loggerButton);

    QPushButton *writeToFileButton = new QPushButton("Write to file");
    toolbarLayout->addWidget(writeToFileButton);

    toolbarLayout->addStretch();

    _splitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(_splitter, 10);

    _siTreeWidget = new HduiSceneIndexTreeWidget;
    _splitter->addWidget(_siTreeWidget);

    _dsTreeWidget = new HduiDataSourceTreeWidget;
    _splitter->addWidget(_dsTreeWidget);

    _valueTreeView = new HduiDataSourceValueTreeView;
    _splitter->addWidget(_valueTreeView);

    QObject::connect(_siTreeWidget, &HduiSceneIndexTreeWidget::PrimSelected, [this](const SdfPath &primPath, HdContainerDataSourceHandle dataSource) {
        this->_valueTreeView->SetDataSource(nullptr);
        this->_dsTreeWidget->SetPrimDataSource(primPath, dataSource);
    });

    QObject::connect(_dsTreeWidget, &HduiDataSourceTreeWidget::DataSourceSelected,
                     [this](HdDataSourceBaseHandle dataSource) { this->_valueTreeView->SetDataSource(HdSampledDataSource::Cast(dataSource)); });

    QObject::connect(_siTreeWidget, &HduiSceneIndexTreeWidget::PrimDirtied, [this](const SdfPath &primPath, const HdDataSourceLocatorSet &locators) {
        HdSceneIndexPrim prim = this->_currentSceneIndex->GetPrim(primPath);
        this->_dsTreeWidget->PrimDirtied(primPath, prim.dataSource, locators);
    });

    QObject::connect(_siChooser, &HduiRegisteredSceneIndexChooser::SceneIndexSelected,
                     [this](const std::string &name, HdSceneIndexBaseRefPtr sceneIndex) { this->SetRegisteredSceneIndex(name, sceneIndex); });

    if (_goToInputButtonMenu)
    {
        QObject::connect(_goToInputButtonMenu, &QMenu::aboutToShow, this, &HduiSceneIndexDebuggerWidget::_FillGoToInputMenu);
    }

    QObject::connect(loggerButton, &QPushButton::clicked, [this]() {
        HduiSceneIndexObserverLoggingWidget *loggingWidget = new HduiSceneIndexObserverLoggingWidget();

        loggingWidget->SetLabel(_nameLabel->text().toStdString());
        loggingWidget->show();
        if (this->_currentSceneIndex)
        {
            loggingWidget->GetTreeView()->SetSceneIndex(this->_currentSceneIndex);
        }
    });
#if PXR_VERSION >= 2408
    QObject::connect(writeToFileButton, &QPushButton::clicked, [this]() {
        const HdSceneIndexBaseRefPtr si = this->_currentSceneIndex;
        if (si)
        {
            const std::string fileNamePrefix = "si_" + si->GetDisplayName() + "_";

            std::string filePath;
            if (ArchMakeTmpFile(fileNamePrefix, &filePath) == -1)
            {
                OPENDCC_ERROR("Could not create file to write out scene index.");
                return;
            }

            // XXX May be useful to allow a subtree to be dumped.
            //     For now, use the absolute root.
            const SdfPath &rootPath = SdfPath::AbsoluteRootPath();

            std::fstream output(filePath, std::ios::out);
            HdUtils::PrintSceneIndex(output, si, rootPath);
            output.close();

            std::cerr << "Wrote scene index contents to " << filePath << std::endl;
        }
    });
#endif
}

void HduiSceneIndexDebuggerWidget::SetRegisteredSceneIndex(const std::string &registeredName, HdSceneIndexBaseRefPtr sceneIndex)
{
    SetSceneIndex(registeredName, std::move(sceneIndex), true);
}

void HduiSceneIndexDebuggerWidget::SetSceneIndex(const std::string &displayName, HdSceneIndexBaseRefPtr sceneIndex, bool pullRoot)
{
    _currentSceneIndex = sceneIndex;

    bool inputsPresent = false;
    if (HdFilteringSceneIndexBaseRefPtr filteringSi = TfDynamic_cast<HdFilteringSceneIndexBaseRefPtr>(sceneIndex))
    {
        if (!filteringSi->GetInputScenes().empty())
        {
            inputsPresent = true;
        }
    }

    if (_goToInputButton)
    {
        _goToInputButton->setEnabled(inputsPresent);
    }

    std::ostringstream buffer;
    if (sceneIndex)
    {
        buffer << "<b><i>(";
#if PXR_VERSION >= 2408
        buffer << sceneIndex->GetDisplayName();
#else
        std::string typeName = typeid(*sceneIndex).name();
        buffer << Hdui_StripNumericPrefix(typeName);
#endif
        buffer << ")</i></b> ";
    }
    buffer << displayName;

    _nameLabel->setText(buffer.str().c_str());

    this->_nameLabel->setText(buffer.str().c_str());
    this->_dsTreeWidget->SetPrimDataSource(SdfPath(), nullptr);
    this->_valueTreeView->SetDataSource(nullptr);

    _siTreeWidget->SetSceneIndex(sceneIndex);

    if (pullRoot)
    {
        _siTreeWidget->Requery();
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

void HduiSceneIndexDebuggerWidget::_FillGoToInputMenu()
{
    QMenu *menu = _goToInputButtonMenu;
    menu->clear();

    QSizePolicy policy = menu->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Expanding);
    menu->setSizePolicy(policy);

    QTreeWidget *menuTreeWidget = new QTreeWidget;
    menuTreeWidget->setHeaderHidden(true);
    menuTreeWidget->setAllColumnsShowFocus(true);
    menuTreeWidget->setMouseTracking(true);
    menuTreeWidget->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);

    QObject::connect(menuTreeWidget, &QTreeWidget::itemEntered, [menuTreeWidget](QTreeWidgetItem *item, int column) {
        menuTreeWidget->setCurrentItem(item, 0, QItemSelectionModel::Select | QItemSelectionModel::Clear);
    });

    QObject::connect(menuTreeWidget, &QTreeWidget::itemClicked, [this, menu](QTreeWidgetItem *item, int column) {
        if (_InputSelectionItem *selectionItem = dynamic_cast<_InputSelectionItem *>(item))
        {

            this->SetSceneIndex("", selectionItem->sceneIndex, true);
            menu->close();
        }
    });

    _AddSceneIndexToTreeMenu(menuTreeWidget->invisibleRootItem(), _currentSceneIndex, false);

    QWidgetAction *widgetAction = new QWidgetAction(menu);
    widgetAction->setDefaultWidget(menuTreeWidget);
    menu->addAction(widgetAction);
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
