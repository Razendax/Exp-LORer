"""Coverage for "This PC", the synthetic virtual navigation root listing every local/remote drive
plus quick-access folders (Architecture.md §14.15).

Uses the `app` fixture (fresh instance per test) -- these tests assert on the exact drive-row set
and on Up's enabled state at the root, which a shared instance's leftover navigation state could
perturb.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

from framework.app import ExpLorerApp
from framework.details_view import double_click_row, read_details_rows
from framework.wait import wait_until

_DRIVE_SUFFIX_RE = re.compile(r"\(([A-Z]):\)$")


def _local_drive_letters() -> list:
    return sorted(chr(c) for c in range(ord("A"), ord("Z") + 1) if os.path.exists(f"{chr(c)}:\\"))


def test_this_pc_lists_exactly_the_systems_drives(app: ExpLorerApp, tmp_path: Path) -> None:
    drive_root = Path(tmp_path).drive + "\\"
    app.navigation.navigate_to(drive_root)

    app.navigation.up.click()
    wait_until(lambda: app.navigation.address_bar.text == "This PC")

    rows = read_details_rows(app.panes.a.file_tree_view)
    drive_letters_shown = sorted(
        match.group(1) for match in (_DRIVE_SUFFIX_RE.search(row.name) for row in rows) if match
    )
    assert drive_letters_shown == _local_drive_letters()

    # Fits naturally here rather than as a separate test method: already at "This PC" with no
    # extra navigation needed. (test_navigation.py's own test_up_disabled_at_this_pc_root already
    # covers this from the default/fresh-launch state -- this just confirms it holds when arrived
    # at via Up from a drive root too, not a second full test of the same thing.)
    assert not app.navigation.up.is_enabled()


def test_double_clicking_a_drive_from_this_pc_navigates_to_its_root(app: ExpLorerApp, tmp_path: Path) -> None:
    drive_letter = Path(tmp_path).drive[0]  # e.g. "C" from "C:"
    drive_root = f"{drive_letter}:\\"

    app.navigation.navigate_to(drive_root)
    app.navigation.up.click()
    wait_until(lambda: app.navigation.address_bar.text == "This PC")

    rows = read_details_rows(app.panes.a.file_tree_view)
    drive_row_name = next(row.name for row in rows if _DRIVE_SUFFIX_RE.search(row.name) and row.name.endswith(f"({drive_letter}:)"))

    double_click_row(app.panes.a.file_tree_view, drive_row_name)
    wait_until(lambda: app.navigation.address_bar.text == drive_root)
