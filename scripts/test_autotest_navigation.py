#!/usr/bin/env python3
"""Regression tests for deterministic Geometry Dash menu navigation."""
from __future__ import annotations

import unittest

from autotest_ab import main_menu_level_editor_icon_center, map_screenshot_point_to_input


class MainMenuNavigationTests(unittest.TestCase):
    def test_level_editor_target_matches_landscape_menu_icon(self) -> None:
        self.assertEqual(main_menu_level_editor_icon_center(1920, 1080), (1325, 432))

    def test_target_scales_with_screenshot_resolution(self) -> None:
        self.assertEqual(main_menu_level_editor_icon_center(1280, 720), (883, 288))

    def test_portrait_panel_is_mapped_to_landscape_input_space(self) -> None:
        x, y, width, height = map_screenshot_point_to_input(
            1325, 432, 1920, 1080, 1080, 1920
        )
        self.assertEqual((x, y, width, height), (1325, 432, 1920, 1080))


if __name__ == "__main__":
    unittest.main()
