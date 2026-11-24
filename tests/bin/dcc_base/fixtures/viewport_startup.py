# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import json
from pathlib import Path
import sys

from Qt import QtCore
from pxr import Gf, Usd, UsdGeom
import opendcc.core as core

app = core.Application.instance()
session = app.get_session()
panel = app.get_main_window().findChild(core.ViewportPanel)
camera_path = "/SceneCamera"
navigation_position = Gf.Vec3d(7, 8, 9)

if sys.argv[1] == "save":
    stage = Usd.Stage.CreateNew(sys.argv[2])
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
    camera = UsdGeom.Camera.Define(stage, camera_path)
    camera.AddTranslateOp().Set(Gf.Vec3d(13, 17, 23))
    stage.GetRootLayer().Save()
    session.set_current_stage(stage)
    panel.set_layout_mode(core.ViewportPanel.LayoutMode.FOUR_PANES)
    # Exercise a pane whose OpenGL widget has never been shown on restore.
    top = panel.get_pane(0).get_viewport_view()
    navigation = top.get_navigation_camera()
    navigation.transform = Gf.Matrix4d(navigation.transform).SetTranslateOnly(navigation_position)
    top.set_navigation_camera(navigation)
    panel.get_pane(2).get_viewport_view().look_through(camera.GetPath())
    panel.maximize_pane(0)
    core.save_layout(str(Path(app.get_settings_path()) / "usd_editor" / "dcc_base.ini"))
else:
    if sys.argv[1] == "open":
        assert session.get_current_stage() is None
        pending = json.loads(bytes(panel.save_state()))
        assert pending["panes"][2]["follow_path"] == camera_path
        session.open_stage(sys.argv[2])
    stage = session.get_current_stage()
    camera = UsdGeom.Camera.Get(stage, camera_path)
    assert camera
    assert panel.is_maximized()
    panel.restore_panes()
    QtCore.QCoreApplication.processEvents()

    top = panel.get_pane(0).get_viewport_view()
    front = panel.get_pane(2).get_viewport_view()
    assert top.get_navigation_camera().transform.ExtractTranslation() == navigation_position
    assert front.get_camera().transform == camera.GetCamera().transform
    state = json.loads(bytes(panel.save_state()))
    assert state["panes"][2]["follow_path"] == camera_path
