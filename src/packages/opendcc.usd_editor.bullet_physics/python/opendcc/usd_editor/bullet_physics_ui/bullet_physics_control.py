# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import os
from pxr import Usd, Tf
from Qt import QtCore, QtGui, QtWidgets
import opendcc
import opendcc.core as dcc_core
import opendcc.usd_editor.bullet_physics as bullet
from . import reset_pivots_and_normalize_extents


class BulletPhysicsControlInstance:
    def __init__(self, on_button, simulation_button, mesh_approximation, buttons):
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.timeout)
        self.on_button = on_button
        self.simulation_button = simulation_button
        self.mesh_approximation = mesh_approximation
        self.buttons = buttons

    def add_dyn(self):
        if not bullet.session().is_enabled():
            return

        engine = bullet.session().current_engine()
        if engine:
            selected_action = self.mesh_approximation.checkedAction().text()
            reset_pivots_and_normalize_extents.reset_pivots_and_normalize_extents_shift_in_selected_objects(
                selected_action == "BOX"
            )
            if selected_action == "BOX":
                dyn_object_type = bullet.MeshApproximationType.BOX
            elif selected_action == "CONVEX_DECOMPOSITION":
                dyn_object_type = bullet.MeshApproximationType.VHACD
            else:
                dyn_object_type = bullet.MeshApproximationType.CONVEX_HULL

            engine.create_selected_prims_as_dynamic_object(dyn_object_type)
            del engine

    def add_static(self):
        if not bullet.session().is_enabled():
            return
        engine = bullet.session().current_engine()
        if engine:
            engine.create_selected_prims_as_static_object()
            del engine

    def remove_bodies(self):
        engine = bullet.session().current_engine()
        if engine:
            engine.remove_selected_prims_from_dym_scene()
            del engine

    def remove_all_bodies(self):
        engine = bullet.session().current_engine()
        if engine:
            engine.remove_all()
            del engine

    def debug_draw(self, checked):
        bullet.set_debug_draw_enabled(checked)

    def start_simulation(self, checked):
        if checked:
            self.simulation_button.setIcon(QtGui.QIcon(":/icons/bullet_stop"))
            self.timer.start(1000.0 / 24.0)
        else:
            self.simulation_button.setIcon(QtGui.QIcon(":/icons/bullet_play"))
            self.timer.stop()

    def timeout(self):
        self.step_simulation(True)

    def relax(self):
        engine = bullet.session().current_engine()
        if engine:
            engine.create_selected_prims_as_dynamic_object(bullet.MeshApproximationType.CONVEX_HULL)
            self.step_simulation(False)
            del engine

    def activate(self, checked):
        engine = bullet.session().current_engine()
        if not checked:
            if engine:
                bullet.session().set_enabled(False)
            self.on_button.setIcon(QtGui.QIcon(":/icons/bullet_on"))
            for button in self.buttons:
                button.setEnabled(False)
            self.start_simulation(False)
            self.simulation_button.setChecked(False)
        else:
            if engine:
                bullet.session().set_enabled(True)
            self.on_button.setIcon(QtGui.QIcon(":/icons/bullet_off"))
            for button in self.buttons:
                button.setEnabled(True)
        if engine:
            del engine

    def step_simulation(self, add_gravity):
        engine = bullet.session().current_engine()
        if engine:
            engine.step_simulation(add_gravity)
            del engine


# bullet_physics_control = BulletPhysicsControl()
# bullet_physics_control.show()
