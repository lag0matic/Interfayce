"""SteamVR Input registration and action polling for Interfayce."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path


APP_KEY = "com.lag0matic.interfayce"
ACTION_SET = "/actions/interfayce"
LEFT_DRAG_ACTION = "/actions/interfayce/in/left_drag"
RIGHT_DRAG_ACTION = "/actions/interfayce/in/right_drag"


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def action_manifest_path() -> Path:
    return project_root() / "assets" / "steamvr" / "actions.json"


def application_manifest_path() -> Path:
    return project_root() / "interfayce.vrmanifest"


@dataclass(frozen=True)
class DragActionState:
    left_active: bool
    right_active: bool
    left_changed: bool
    right_changed: bool


class SteamVrInput:
    """Owns a live, registered Interfayce action set for one process."""

    def __init__(self) -> None:
        import openvr

        self._openvr = openvr
        openvr.init(openvr.VRApplication_Overlay)
        try:
            applications = openvr.VRApplications()
            applications.addApplicationManifest(str(application_manifest_path()), False)
            applications.identifyApplication(os.getpid(), APP_KEY)
            self._input = openvr.VRInput()
            self._input.setActionManifestPath(str(action_manifest_path()))
            self._action_set = self._input.getActionSetHandle(ACTION_SET)
            self._left_drag = self._input.getActionHandle(LEFT_DRAG_ACTION)
            self._right_drag = self._input.getActionHandle(RIGHT_DRAG_ACTION)
        except Exception:
            openvr.shutdown()
            raise

    def read_drag_actions(self) -> DragActionState:
        active_set = self._openvr.VRActiveActionSet_t()
        active_set.ulActionSet = self._action_set
        self._input.updateActionState(active_set)
        left = self._input.getDigitalActionData(self._left_drag, self._openvr.k_ulInvalidInputValueHandle)
        right = self._input.getDigitalActionData(self._right_drag, self._openvr.k_ulInvalidInputValueHandle)
        return DragActionState(
            left_active=bool(left.bState),
            right_active=bool(right.bState),
            left_changed=bool(left.bChanged),
            right_changed=bool(right.bChanged),
        )

    def close(self) -> None:
        self._openvr.shutdown()
