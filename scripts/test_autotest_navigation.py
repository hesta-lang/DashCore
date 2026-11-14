#!/usr/bin/env python3
"""Regression tests for deterministic Geometry Dash menu navigation."""
from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import patch

from autotest_ab import (
    main_menu_level_editor_icon_center,
    map_screenshot_point_to_input,
    tap_saved_levels_button,
)


class MainMenuNavigationTests(unittest.TestCase):
    def test_level_editor_target_matches_landscape_menu_icon(self) -> None:
        self.assertEqual(main_menu_level_editor_icon_center(1920, 1080), (1325, 432))

    def test_target_scales_with_screenshot_resolution(self) -> None:
        self.assertEqual(main_menu_level_editor_icon_center(1280, 720), (883, 288))

    def test_saved_levels_button_uses_geometry_dash_22081_label(self) -> None:
        directory = Path("reports")
        with patch("autotest_ab.tap_ocr_phrase", return_value={"phrase": "Saved"}) as tap:
            result = tap_saved_levels_button("emulator-5554", "DashCore", directory)
        self.assertEqual(result, {"phrase": "Saved"})
        tap.assert_called_once_with(
            "emulator-5554", "DashCore", directory, "open-saved-levels", "Saved", 25
        )

    def test_portrait_panel_is_mapped_to_landscape_input_space(self) -> None:
        x, y, width, height = map_screenshot_point_to_input(
            1325, 432, 1920, 1080, 1080, 1920
        )
        self.assertEqual((x, y, width, height), (1325, 432, 1920, 1080))


if __name__ == "__main__":
    unittest.main()
