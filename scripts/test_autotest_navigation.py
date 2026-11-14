#!/usr/bin/env python3
"""Regression tests for deterministic Geometry Dash menu navigation."""
from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

from autotest_ab import (
    create_local_levels_button_center,
    main_menu_level_editor_icon_center,
    map_screenshot_point_to_input,
    tap_create_local_levels_button,
)


class MainMenuNavigationTests(unittest.TestCase):
    def test_level_editor_target_matches_landscape_menu_icon(self) -> None:
        self.assertEqual(main_menu_level_editor_icon_center(1920, 1080), (1325, 432))

    def test_target_scales_with_screenshot_resolution(self) -> None:
        self.assertEqual(main_menu_level_editor_icon_center(1280, 720), (883, 288))

    def test_create_local_levels_tile_center_matches_v22081_menu(self) -> None:
        self.assertEqual(create_local_levels_button_center(1920, 1080), (303, 204))
        self.assertEqual(create_local_levels_button_center(1280, 720), (202, 136))

    def test_create_local_levels_button_is_tapped_not_saved(self) -> None:
        import tempfile
        import json

        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            commands = []

            def screenshot(_serial, _mode, _label, destination):
                Image.new("RGB", (1920, 1080)).save(destination)
                return "CREATE SAVED SCORES"

            def fake_shell(_serial, command, check=True, timeout=90):
                commands.append(command)
                if command == "wm size":
                    return "Physical size: 1920x1080"
                return ""

            with patch("autotest_ab.take_state_screenshot", side_effect=screenshot), patch("autotest_ab.shell", side_effect=fake_shell), patch("autotest_ab.time.sleep"):
                event = tap_create_local_levels_button(
                    "emulator-5554", "DashCore", directory
                )

            self.assertEqual(event["target"], "create-local-levels-button")
            self.assertEqual(event["adb_tap"], [303, 204])
            self.assertIn("input tap 303 204", commands)
            navigation = json.loads(
                (directory / "autotest-level-navigation.json").read_text()
            )
            self.assertEqual(navigation["steps"][0]["target"], "create-local-levels-button")

    def test_portrait_panel_is_mapped_to_landscape_input_space(self) -> None:
        x, y, width, height = map_screenshot_point_to_input(
            1325, 432, 1920, 1080, 1080, 1920
        )
        self.assertEqual((x, y, width, height), (1325, 432, 1920, 1080))


if __name__ == "__main__":
    unittest.main()
