import copy
import json
import math
import tempfile
import threading
import unittest
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from navigation.zones import ZoneStore, Conflict, validate_zones, map_key
from navigation.zone_service import ZoneService
from navigation.sim import Simulation
from navigation.server import Server


def zone(kind="carpet", **kwargs):
    value = dict(id="rug-1", name="שטיח בסלון", kind=kind, x1=1.0, y1=1.0, x2=1.8, y2=1.8,
                 enabled=True, present=True, boost=True, extra_passes=1)
    value.update(kwargs)
    return value


class MapZonesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.sim = Simulation("room")
        self.key = map_key(self.sim.state())
        self.store = ZoneStore(self.tmp.name)

    def body(self, zones=None):
        return dict(key=self.key, revision=0, name="קומת כניסה", zones=zones or [zone()])

    def test_atomic_roundtrip_revision_and_map_identity(self):
        saved = self.store.save(self.key, self.body(), self.sim.state()["cells"])
        self.assertEqual(ZoneStore(self.tmp.name).load(self.key), saved)
        with self.assertRaises(Conflict): self.store.save(self.key, self.body(), [])
        key = copy.deepcopy(self.key); key["geometry"]["origin_x"] = 1
        self.assertEqual(self.store.load(key)["zones"], [])
        with self.assertRaises(Conflict): self.store.save(key, self.body(), [])

    def test_invalid_geometry_and_policy_are_rejected(self):
        for changes in (dict(x1=float("nan")), dict(x2=5), dict(x1=True), dict(extra_passes=3),
                        dict(present="false"), dict(x1=2), dict(kind="unknown")):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                validate_zones([zone(**changes)], self.key["geometry"])
        with self.assertRaises(ValueError): validate_zones([zone(), zone()], self.key["geometry"])

    def test_corrupt_saved_data_fails_closed(self):
        self.store.save(self.key, self.body(), [])
        self.store.path(self.key).write_text('{"version":999}')
        with self.assertRaises(ValueError): self.store.load(self.key)

    def test_dock_and_current_pose_overlap_do_not_persist(self):
        service = ZoneService(self.tmp.name)
        body = self.body([zone("no_go", x1=.6, y1=.6, x2=.8, y2=.8)])
        with self.assertRaises(ValueError): service.save(self.sim.state(), body, self.sim)
        self.assertEqual(service.describe(self.sim.state())["revision"], 0)

    def test_carpet_present_boost_and_additive_passes(self):
        service = ZoneService(self.tmp.name)
        service.save(self.sim.state(), self.body(), self.sim)
        self.sim.mission.start()
        self.assertEqual(self.sim.mission.base_passes, 1)
        self.assertEqual(self.sim.mission.passes, 2)
        from navigation.core import Pose
        self.sim.pose = Pose(1.4, 1.4)
        self.assertEqual(self.sim.mission.tick(self.sim.observation()).cleaning_pwm, 127)
        self.sim.cliff = True
        self.assertEqual(self.sim.mission.tick(self.sim.observation()).cleaning_pwm, 0)
        absent = self.body([zone(present=False)]); absent["revision"] = 1
        other = Simulation("room")
        service.save(other.state(), absent, other)
        other.mission.start()
        self.assertEqual(other.mission.passes, 1)

    def test_api_conflict_restart_and_simulation_only(self):
        server = Server(("127.0.0.1", 0), zones_path=self.tmp.name)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        def cleanup(): server.shutdown(); server.server_close(); thread.join()
        self.addCleanup(cleanup)
        url = "http://127.0.0.1:" + str(server.server_port)
        def post(path, value):
            return json.load(urlopen(Request(url+path, data=json.dumps(value).encode(), headers={"Content-Type":"application/json"})))
        doc = json.load(urlopen(url + "/api/zones"))
        doc["zones"] = [zone()]
        result = post("/api/zones", doc)
        self.assertEqual(result["revision"], 1)
        with self.assertRaises(HTTPError) as e: post("/api/zones", doc)
        self.assertEqual(e.exception.code, 409)
        result = post("/api/reset", {"scenario":"furniture", "profile":"deep"})
        self.assertEqual(len(result["zones"]), 1)
        post("/api/start", {})
        self.assertEqual(json.load(urlopen(url+"/api/state"))["passes"], 3)
        self.assertEqual(post("/api/reset", {"scenario":"room"})["zones"], [])

    def test_zones_survive_charge_resume_during_extra_carpet_pass(self):
        sim = self.sim
        items = [zone(), zone("no_go", id="bowls", x1=2.3, y1=1.3, x2=2.7, y2=1.7)]
        ZoneService(self.tmp.name).save(sim.state(), self.body(items), sim)
        sim.mission.start()
        requested_charge = charged = resumed = boosted = False
        before = None
        passes = set()
        minimum = math.inf
        for step in range(18000):
            sim.step()
            m = sim.mission
            passes.add(m.pass_index)
            distance = math.hypot(max(2.3-sim.pose.x, 0, sim.pose.x-2.7),
                                  max(1.3-sim.pose.y, 0, sim.pose.y-1.7))
            minimum = min(minimum, distance)
            self.assertGreaterEqual(distance, .215, "simulated body entered the exclusion margin")
            self.assertNotEqual(m.state, "FAULT", (step, m.reason))
            boosted |= sim.command.cleaning_pwm == 127 and sim.command.side_pwm == 127
            if m.pass_index == 1 and m.fine_covered[1] and not requested_charge:
                before = set(m.fine_covered[1])
                sim.inject("battery")
                requested_charge = True
            if requested_charge and m.state == "CHARGING":
                charged = True
                self.assertEqual(m.return_cause, "battery")
                self.assertTrue(before <= set(m.fine_covered[1]))
            if charged and m.state == "CLEANING":
                resumed = True
                self.assertEqual(m.pass_index, 1)
                self.assertTrue(before <= set(m.fine_covered[1]))
            if m.state == "COMPLETE":
                break
        self.assertEqual(sim.mission.state, "COMPLETE")
        self.assertTrue(requested_charge and charged and resumed and boosted)
        self.assertEqual(passes, {0, 1})
        self.assertGreater(len(sim.mission.fine_covered[1]), len(before))
        self.assertEqual((sim.collisions, sim.dock_brush_contacts), (0, 0))
        self.assertEqual(sim.mission.zones[1]["id"], "bowls")
        print("combined zones/charge: steps=%d passes=%s clearance=%.6fm extra_samples=%d collisions=%d dock_contacts=%d" %
              (step+1, sorted(passes), minimum, len(sim.mission.fine_covered[1]), sim.collisions, sim.dock_brush_contacts), flush=True)


if __name__ == "__main__": unittest.main()
