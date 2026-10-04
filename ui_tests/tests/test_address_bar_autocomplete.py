"""Coverage for the pane address bar's folder-suggestion popup and inline autocomplete
(`AddressBarWidget`, Architecture.md §14.21).

Uses `ExpLorerFreshAppTestCase`: navigation history/address bar state is window-wide, and these
tests assert on exact address-bar text, which a shared instance's leftover state could perturb.
"""

from __future__ import annotations

from pathlib import Path

from framework import ExpLorerFreshAppTestCase
from framework.wait import wait_until


class TestAddressBarAutocomplete(ExpLorerFreshAppTestCase):
    def test_unambiguous_partial_path_inline_completes_to_the_matching_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "Documents").mkdir()
        (root / "Downloads").mkdir()

        self.app.navigation.navigate_to(str(root))
        nav = self.app.navigation
        # "Doc" only matches "Documents" -- the inline ghost-completion suffix is unambiguous.
        nav.type_partial(str(root) + "\\Doc")

        wait_until(lambda: nav.address_bar.text == str(root / "Documents"))

    def test_no_match_leaves_typed_text_unchanged(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "Documents").mkdir()

        self.app.navigation.navigate_to(str(root))
        nav = self.app.navigation
        typed = str(root) + "\\Zzz"
        nav.type_partial(typed)

        wait_until(lambda: nav.address_bar.text == typed)

    def test_enter_navigates_to_the_inline_completed_folder(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "Documents").mkdir()

        self.app.navigation.navigate_to(str(root))
        nav = self.app.navigation
        nav.type_partial(str(root) + "\\Doc")
        wait_until(lambda: nav.address_bar.text == str(root / "Documents"))

        nav.address_bar.raw.type_keys("{ENTER}")
        wait_until(lambda: nav.address_bar.text == str(root / "Documents"))

    def test_non_ascii_folder_name_autocompletes(self, tmp_path: Path) -> None:
        root = tmp_path / "root"
        root.mkdir()
        (root / "Пример").mkdir()  # Cyrillic
        (root / "Документы").mkdir()  # Cyrillic, shares the "До" prefix with nothing else here

        self.app.navigation.navigate_to(str(root))
        nav = self.app.navigation
        nav.type_partial(str(root) + "\\Докум")

        wait_until(lambda: nav.address_bar.text == str(root / "Документы"))
