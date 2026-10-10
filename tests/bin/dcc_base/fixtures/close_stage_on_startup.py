# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import opendcc.core as core
from pxr import Sdf, Usd

app = core.Application.instance()
assert app.is_ui_available()
app.set_current_viewport_tool(None)
session = app.get_session()

# Run before yielding to Qt: closing must work without a startup paint making GL current.
for _ in range(2):
    stage = Usd.Stage.CreateInMemory()
    session.set_current_stage(stage)
    with core.UsdEditsUndoBlock():
        stage.DefinePrim(Sdf.Path("/World"))
    app.get_undo_stack().clear()
    assert session.close_stage(stage)
    assert session.get_current_stage() is None
    assert session.get_stage_list() == []

print("startup stages closed", flush=True)
