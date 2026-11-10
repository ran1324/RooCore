"""Dashboard integration. Robot/replay documents cannot arm a motion path."""
from .zones import ZoneStore, map_key, validate_zones


class ZoneService:
    def __init__(self, directory):
        self.store = ZoneStore(directory)
        self.applied = None

    def describe(self, state):
        key = map_key(state)
        doc = self.store.load(key)
        doc["application"] = "simulation" if state.get("source", "simulation") == "simulation" else "annotations_only"
        doc["editable"] = state["state"] in ("READY", "PAUSED", "COMPLETE")
        return doc

    def activate(self, sim):
        key = map_key(sim.state())
        doc = self.store.load(key)
        sim.mission.set_zones(doc["zones"])
        self.applied = (key, doc["revision"])

    def save(self, state, body, sim=None):
        key = map_key(state)
        if state["state"] not in ("READY", "PAUSED", "COMPLETE"):
            raise ValueError("pause cleaning before changing zones")
        zones = validate_zones(body.get("zones"), key["geometry"])
        if sim:
            # Validate blocking geometry against current pose and the dock BEFORE
            # persistence. Do not strand the robot inside a newly drawn obstacle.
            sim.mission.validate_zones(zones, sim.pose)
        doc = self.store.save(key, body, state["cells"])
        if sim:
            sim.mission.set_zones(doc["zones"])
            self.applied = (key, doc["revision"])
        return self.describe(state)
