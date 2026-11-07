import importlib.util
from pathlib import Path
import time
import unittest

path = Path(__file__).resolve().parents[1] / 'ros2/roocore_bridge/roocore_bridge/client.py'
spec = importlib.util.spec_from_file_location('robot_client', path)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)


class ClientTests(unittest.TestCase):
    def client(self):
        client = module.RobotClient('http://127.0.0.1', 'x' * 24)
        client.status = {'owner': 1, 'now': 1000, 'epoch': 4}
        client.epoch = 4; client.received = time.monotonic()
        client.requests = []
        client._request = lambda path, fields: client.requests.append((path, fields))
        return client

    def test_velocity_uses_mcu_clock_and_sequence(self):
        client = self.client(); self.assertTrue(client.velocity(.1, 0))
        payload = client.requests[0][1]
        self.assertEqual(payload['expires'], 1200)
        self.assertEqual(payload['epoch'], 4)
        self.assertEqual(payload['left'], 100)
        client.velocity(0, 0); self.assertEqual(client.requests[1][1]['sequence'], 2)

    def test_no_stale_velocity_or_local_owner(self):
        client = self.client(); client.received -= 1
        self.assertFalse(client.velocity(.1, 0)); self.assertEqual(client.requests, [])
        client.received = time.monotonic(); client.status['owner'] = 0
        self.assertFalse(client.velocity(.1, 0))

    def test_rejects_invalid_or_over_limit_commands(self):
        client = self.client()
        for speed, turn in ((float('nan'), 0), (.5, 0), (0, 10)):
            with self.assertRaises(ValueError): client.velocity(speed, turn)
        self.assertEqual(client.requests, [])

    def test_tick_wrap(self):
        client = self.client(); client.status['now'] = 0xfffffff0
        client.velocity(0, 0); self.assertEqual(client.requests[0][1]['expires'], 184)
