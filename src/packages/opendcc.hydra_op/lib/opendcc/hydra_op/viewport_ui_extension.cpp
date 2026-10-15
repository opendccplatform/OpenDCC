// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/hydra_op/viewport_ui_extension.h"
#include <QWidget>
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_overlay.h"
#include "opendcc/app/ui/application_ui.h"
#include <QActionGroup>
#include <opendcc/base/commands_api/core/command_registry.h>
#include <opendcc/base/commands_api/core/command_interface.h>
#include <opendcc/app/viewport/viewport_isolate_selection_command.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

HydraOpViewportUIExtension::HydraOpViewportUIExtension(ViewportWidget* viewport_widget)
    : IViewportUIExtension(viewport_widget)
{
    std::function<void()> view_node_changed = [this] {
        const QString terminal_node = HydraOpSession::instance().get_view_node().GetText();
    };

    m_view_node_changed_cid = HydraOpSession::instance().register_event_handler(HydraOpSession::EventType::ViewNodeChanged, view_node_changed);
    viewport_widget->add_scene_context(TfToken("HydraOp"));

    QActionGroup* toolbar_hydra_op_context_group = new QActionGroup(viewport_widget);
    toolbar_hydra_op_context_group->setExclusive(false);
    toolbar_hydra_op_context_group->setVisible(false);

    const auto scene_context_changed = [this, toolbar_hydra_op_context_group](TfToken context) {
        if (context == TfToken("HydraOp"))
        {
            /*get_viewport_widget()->clear_camera_menu_controller_actions();
            auto gl_widget = get_viewport_widget()->get_gl_widget();
            get_viewport_widget()->set_camera_menu_controller(new ViewportHydraOpCameraMenuController(
                gl_widget->get_camera_controller(), get_viewport_widget()->get_overlay()->widget(), get_viewport_widget()));
            get_viewport_widget()->fill_camera_menu_controller_actions();*/

            toolbar_hydra_op_context_group->setVisible(true);
        }
        else
        {
            toolbar_hydra_op_context_group->setVisible(false);
        }
    };

    scene_context_changed(viewport_widget->get_scene_context()->get_context_name());
    viewport_widget->connect(viewport_widget, &ViewportWidget::scene_context_changed, scene_context_changed);
    QAction* isolate_selection = new QAction(i18n("viewport.actions", "Isolate Selection"), viewport_widget);
    isolate_selection->setIcon(QIcon(":icons/IsolateSelected.png"));
    isolate_selection->setCheckable(true);
    isolate_selection->setChecked(false);
    viewport_widget->connect(isolate_selection, &QAction::triggered, viewport_widget, [this, isolate_selection](bool checked) {
        SdfPathVector selection;
        if (checked)
        {
            selection = HydraOpSession::instance().get_selection().get_fully_selected_paths();
        }
        auto gl_widget = get_viewport_widget()->get_gl_widget();

        auto isolate_cmd = base::commands::CommandRegistry::create_command<ViewportIsolateSelectionCommand>("isolate");
        isolate_cmd->set_ui_state(gl_widget, [this, checked, gl_widget, isolate_selection](bool undo) {
            if (!gl_widget || !isolate_selection)
                return;

            isolate_selection->setChecked(undo ? !checked : checked);
            gl_widget->update();
        });
        base::commands::CommandInterface::execute(isolate_cmd, base::commands::CommandArgs().kwarg("paths", selection));
    });

    viewport_widget->toolbar_add_action(isolate_selection);
    toolbar_hydra_op_context_group->addAction(isolate_selection);

    m_selection_changed_cid = HydraOpSession::instance().register_event_handler(HydraOpSession::EventType::SelectionChanged, [this] {
        auto gl_widget = get_viewport_widget()->get_gl_widget();
        if (auto engine = gl_widget->get_engine())
        {
            if (gl_widget->get_scene_context_type() == TfToken("HydraOp"))
            {
                engine->set_selected(HydraOpSession::instance().get_selection());
            }
        }
        gl_widget->update();
    });
}

HydraOpViewportUIExtension::~HydraOpViewportUIExtension()
{
    HydraOpSession::instance().unregister_event_handler(HydraOpSession::EventType::SelectionChanged, m_selection_changed_cid);
    HydraOpSession::instance().unregister_event_handler(HydraOpSession::EventType::ViewNodeChanged, m_view_node_changed_cid);
}

std::vector<IViewportDrawExtensionPtr> HydraOpViewportUIExtension::create_draw_extensions()
{
    return {};
}

OPENDCC_NAMESPACE_CLOSE
