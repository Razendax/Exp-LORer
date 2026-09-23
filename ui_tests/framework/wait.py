"""Small polling helper for UI state that updates asynchronously -- background navigation
(TabViewModel::navigateTo dispatches off the UI thread), DirectoryWatchService's debounced
live-refresh, and similar -- instead of ad-hoc `time.sleep()` guesses scattered across tests.
"""

from __future__ import annotations

import time
from typing import Callable, Optional, TypeVar

T = TypeVar("T")


def wait_until(predicate: Callable[[], T], timeout: float = 5.0, interval: float = 0.2) -> T:
    """Polls `predicate` every `interval` seconds until it returns a truthy value, then returns it.

    An exception raised by `predicate` is treated like a falsy result and retried (a widget can be
    transiently unreachable mid-rebuild) -- except once `timeout` has elapsed, when it's re-raised
    so the failure points at the real cause instead of a generic timeout message.
    """
    deadline = time.monotonic() + timeout
    last_result: Optional[T] = None
    last_error: Optional[BaseException] = None
    while time.monotonic() < deadline:
        try:
            last_result = predicate()
            last_error = None
            if last_result:
                return last_result
        except Exception as exc:  # noqa: BLE001 -- deliberately broad, see docstring
            last_error = exc
        time.sleep(interval)

    if last_error is not None:
        raise TimeoutError(f"Condition not met within {timeout}s") from last_error
    raise TimeoutError(f"Condition not met within {timeout}s (last value: {last_result!r})")
