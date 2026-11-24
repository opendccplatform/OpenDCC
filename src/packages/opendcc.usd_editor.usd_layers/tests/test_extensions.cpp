// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_layers/usd_layers_model.h"
#include "opendcc/usd_editor/usd_layers/usd_layers_view.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    class ExtendedLayerModel : public UsdLayersModel
    {
    public:
        int columnCount(const QModelIndex& = {}) const override { return ColumnCount + 1; }

        QVariant data(const QModelIndex& index, int role) const override
        {
            if (index.column() == ColumnCount && role == Qt::DisplayRole)
                return QStringLiteral("C++ package column");
            return UsdLayersModel::data(index, role);
        }

        QVariant headerData(int section, Qt::Orientation orientation, int role) const override
        {
            if (section == ColumnCount && orientation == Qt::Horizontal && role == Qt::DisplayRole)
                return QStringLiteral("Package");
            return UsdLayersModel::headerData(section, orientation, role);
        }
    };

}

DOCTEST_TEST_SUITE("UsdLayersExtensions")
{
    DOCTEST_TEST_CASE("C++ model, column and context menu extensions")
    {
        int argc = 1;
        char name[] = "layers_extensions";
        char* argv[] = { name, nullptr };
        QApplication app(argc, argv);
        auto root = SdfLayer::CreateAnonymous("extensions.usda");
        auto stage = UsdStage::Open(root);
        ExtendedLayerModel model;
        model.set_stage(stage);
        UsdLayersView view;
        view.set_layer_model(&model);
        const auto index = view.model()->index(1, UsdLayersModel::ColumnCount);
        DOCTEST_REQUIRE(index.isValid());
        DOCTEST_CHECK(index.data().toString() == QStringLiteral("C++ package column"));
        DOCTEST_CHECK(view.model()->headerData(UsdLayersModel::ColumnCount, Qt::Horizontal).toString() == QStringLiteral("Package"));
        view.resize(1200, 240);
        view.show();
        app.processEvents();
        DOCTEST_CHECK_FALSE(view.grab().isNull());

        bool menu_called = false;
        bool action_called = false;
        QObject::connect(&view, &UsdLayersView::context_menu_requested, &view, [&](QMenu* menu, const QString& identifier, const QString& parent) {
            menu_called = true;
            DOCTEST_CHECK(identifier.toStdString() == root->GetIdentifier());
            DOCTEST_CHECK(parent.isEmpty());
            auto* action = menu->addAction(QStringLiteral("C++ package action"));
            QObject::connect(action, &QAction::triggered, &view, [&]() { action_called = true; });
            QTimer::singleShot(0, menu, [menu, action]() {
                action->trigger();
                menu->close();
            });
        });
        const QPoint position = view.visualRect(view.model()->index(1, 0)).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position, view.viewport()->mapToGlobal(position));
        QApplication::sendEvent(view.viewport(), &event);
        DOCTEST_CHECK(menu_called);
        DOCTEST_CHECK(action_called);
    }
}

OPENDCC_NAMESPACE_CLOSE
