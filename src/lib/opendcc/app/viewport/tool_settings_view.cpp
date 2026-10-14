// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/tool_settings_view.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"
#include <QBoxLayout>
#include <QLabel>
#include <pxr/base/tf/diagnostic.h>
#include "viewport_widget.h"
#include "viewport_view.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.tools");

PXR_NAMESPACE_USING_DIRECTIVE

ToolSettingsView::ToolSettingsView(QWidget* parent /*= nullptr*/)
    : QWidget(parent)
{
    m_tool_changed_handle = Application::instance().register_event_callback(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED,
                                                                            std::bind(&ToolSettingsView::on_viewport_tool_changed, this));
    auto layout = new QVBoxLayout;
    if (auto tool_context = ApplicationUI::instance().get_current_viewport_tool())
    {
        layout->addWidget(new QLabel(QString::fromStdString(tool_context->get_name())));

        if (auto tool_settings_widget = ToolSettingsViewRegistry::create_tool_settings_widget(
                tool_context->get_name(), Application::instance().get_active_view_scene_context()))
        {
            layout->addWidget(tool_settings_widget);
        }
    }
    else
    {
        layout->addWidget(new QLabel(i18n("tool_settings", "There is no active tool.")));
    }
    layout->addStretch();
    setLayout(layout);
}

ToolSettingsView::~ToolSettingsView()
{
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED, m_tool_changed_handle);
}

void ToolSettingsView::on_viewport_tool_changed()
{
    auto layout = qobject_cast<QVBoxLayout*>(this->layout());
    if (layout->count() > 2)
        delete layout->takeAt(1)->widget();

    QString tool_name_str;
    if (auto context = ApplicationUI::instance().get_current_viewport_tool())
    {
        tool_name_str = QString::fromStdString(context->get_name());
        if (auto tool_settings_widget =
                ToolSettingsViewRegistry::create_tool_settings_widget(context->get_name(), Application::instance().get_active_view_scene_context()))
        {
            layout->insertWidget(1, tool_settings_widget);
        }
    }
    else
    {
        tool_name_str = i18n("tool_settings", "There is no active tool.");
    }

    if (auto tool_name = qobject_cast<QLabel*>(layout->itemAt(0)->widget()))
        tool_name->setText(tool_name_str);
    else
        OPENDCC_ERROR("Tool name widget is not initialized.");
}

ToolSettingsViewRegistry& ToolSettingsViewRegistry::instance()
{
    static ToolSettingsViewRegistry instance;
    return instance;
}

bool ToolSettingsViewRegistry::register_tool_settings_view(const TfToken& name, const TfToken& context, std::function<QWidget*()> factory_fn)
{
    if (!factory_fn)
    {
        OPENDCC_ERROR("Failed to register tool settings widget '{}': factory is empty.", name.GetText());
        return false;
    }
    auto& self = instance();

    auto& registry = self.m_registry[context];
    auto iter = registry.find(name);
    if (iter != registry.end())
    {
        OPENDCC_ERROR("Tool settings widget with name '{}' was already registered for context '{}'.", name.GetText(), context.GetText());
        return false;
    }
    else
        registry[name] = factory_fn;
    return true;
}

bool ToolSettingsViewRegistry::unregister_tool_settings_view(const TfToken& name, const TfToken& context)
{
    auto& self = instance();

    auto context_iter = self.m_registry.find(context);
    if (context_iter == self.m_registry.end())
        return false;
    auto& registry = context_iter->second;
    return registry.erase(name);
}

QWidget* ToolSettingsViewRegistry::create_tool_settings_widget(const TfToken& name, const TfToken& context)
{
    auto& registry = instance().m_registry;

    auto context_iter = registry.find(context);
    if (context_iter == registry.end())
    {
        OPENDCC_DEBUG("Failed to create tool settings widget with name '{}' for context '{}'.", name.GetText(), context.GetText());
        return nullptr;
    }

    auto fn_iter = context_iter->second.find(name);
    if (fn_iter == context_iter->second.end())
    {
        OPENDCC_DEBUG("Failed to create tool settings widget with name '{}' for context '{}'.", name.GetText(), context.GetText());
        return nullptr;
    }
    return fn_iter->second();
}

OPENDCC_NAMESPACE_CLOSE
