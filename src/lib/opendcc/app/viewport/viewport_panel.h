/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include <QByteArray>
#include <QWidget>
#include <QPointer>
#include <array>

class QSplitter;
class QFrame;
class QAction;

OPENDCC_NAMESPACE_OPEN

class ViewportWidget;

/** @brief Dock panel containing independent views of the current USD stage. */
class OPENDCC_API ViewportPanel : public QWidget
{
    Q_OBJECT
public:
    enum class LayoutMode
    {
        SINGLE, ///< Display the active pane.
        FOUR_PANES, ///< Display all four panes.
        TWO_STACKED, ///< Display Top and Perspective vertically.
        TWO_SIDE_BY_SIDE ///< Display Top and Perspective horizontally.
    };

    explicit ViewportPanel(QWidget* parent = nullptr);
    ~ViewportPanel() override;
    void set_layout_mode(LayoutMode mode);
    LayoutMode get_layout_mode() const { return m_mode; }
    int get_active_pane() const { return m_active_pane; }
    void set_active_pane(int index);
    /** @return A pane widget, or nullptr if the pane has not been created. */
    ViewportWidget* get_pane(int index) const;
    void maximize_pane(int index);
    void restore_panes();
    bool is_maximized() const { return m_maximized; }
    /** @brief Capture the pane layout and local navigation cameras. */
    QByteArray save_state() const;
    /** @return False for unsupported or invalid state; old layouts need no state. */
    bool restore_state(const QByteArray& state);

protected:
    void showEvent(QShowEvent* event) override;

private:
    static constexpr int s_pane_count = 4;
    static constexpr int s_row_count = 2;
    static constexpr int s_layout_count = 4;
    static constexpr int s_perspective_pane = 1;

    struct Pane
    {
        QPointer<ViewportWidget> m_widget;
        QPointer<QFrame> m_frame;
        std::array<QPointer<QAction>, s_layout_count> m_layout_actions {};
        QPointer<QAction> m_maximize_action;
    };

    void create_pane(int index);
    void create_pane_actions(int index);
    void update_layout();
    void update_actions();
    bool supports_multiple_views() const;
    void remember_splitters();

    std::array<Pane, s_pane_count> m_panes {};
    QSplitter* m_splitter;
    std::array<QSplitter*, s_row_count> m_rows {};
    std::array<std::array<QByteArray, s_row_count + 1>, s_layout_count> m_splitter_states {};
    LayoutMode m_mode = LayoutMode::SINGLE;
    int m_active_pane = s_perspective_pane;
    bool m_maximized = false;
    bool m_syncing_splitters = false;
    bool m_updating_layout = false;
};

OPENDCC_NAMESPACE_CLOSE
