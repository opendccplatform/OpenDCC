# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0
from pxr import Sdf
from opendcc.usd_editor.usd_outliner import usd_item_id, usd_path_of
from outliner_test_utils import OutlinerTest


class TestBindings(OutlinerTest):
    def test_python_usd_conversions_preserve_types_and_nullable_stage(self):
        source = self.widget.data_source()
        self.assertEqual(source.stage, self.session.get_current_stage())
        path = Sdf.Path("/Root/日本語_Étage_Объект_عقدة_𐐀")
        self.assertEqual(usd_path_of(usd_item_id(path)), path)
        with self.assertRaises(TypeError):
            source.set_stage(path)
        self.assertEqual(source.stage, self.stage)
        for invalid in (None, self.stage):
            with self.subTest(path_value=invalid):
                with self.assertRaises(TypeError):
                    usd_item_id(invalid)
        source.set_stage(None)
        self.assertIsNone(source.stage)
        source.set_stage(self.session.get_current_stage())
        self.assertEqual(source.stage, self.stage)
