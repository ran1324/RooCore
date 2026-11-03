import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from oi import Stream, Receiver, HEADER, delta16
from analyze import summarize


def frame(payload):
    data = bytes([19,len(payload)]) + bytes(payload)
    return data + bytes([-sum(data) & 255])


def transport(seq, body, overrun=0, session=42):
    return HEADER.pack(b'RC01',session,seq,seq*15,overrun) + body

class DecodeTests(unittest.TestCase):
    def test_split_frames_signed_values_and_multiple_frames(self):
        raw = frame([7,3,23,255,156,24,251,43,255,254,44,0,2])
        decoder = Stream()
        rows = []
        for byte in b'boot noise' + raw + raw:
            rows.extend(decoder.feed(bytes([byte])))
        self.assertEqual(len(rows),2)
        self.assertEqual(rows[0][1]['current_ma'],-100)
        self.assertEqual(rows[0][1]['temperature_c'],-5)
        self.assertEqual(rows[0][1]['encoder_left'],65534)

    def test_corruption_and_bad_length_resync(self):
        raw = frame([7,1])
        broken = raw[:-1]+bytes([raw[-1]^1])
        decoder = Stream()
        self.assertEqual(len(decoder.feed(b'\x13\xff'+broken+raw)),1)
        self.assertGreaterEqual(decoder.errors,2)

    def test_unknown_and_truncated_payload_not_accepted(self):
        decoder = Stream()
        self.assertEqual(decoder.feed(frame([99,0])+frame([43,0])),[])
        self.assertEqual(decoder.errors,2)

    def test_loss_clears_partial_frame(self):
        raw = frame([7,1])
        receiver = Receiver(42)
        self.assertEqual(receiver.feed(transport(0,raw[:3])),[])
        rows = receiver.feed(transport(2,raw[3:]+raw))
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]['segment'],1)
        self.assertEqual(receiver.gaps,1)
        self.assertEqual(receiver.feed(transport(1,raw)),[])
        self.assertEqual(receiver.feed(transport(2,raw)),[])

    def test_overrun_new_session_and_short_header(self):
        receiver = Receiver(42)
        receiver.feed(transport(0,b''))
        rows = receiver.feed(transport(1,frame([7,0]),overrun=1))
        self.assertEqual(rows[0]['segment'],1)
        with self.assertRaises(ValueError): receiver.feed(b'RC01')
        with self.assertRaises(ValueError): receiver.feed(transport(2,b'',session=99))

    def test_wrap(self):
        self.assertEqual(delta16(2,65534),4)
        self.assertEqual(delta16(65534,2),-4)
        receiver = Receiver(42)
        receiver.last_seq = 0xffffffff
        self.assertEqual(len(receiver.feed(transport(0,frame([7,0])))),1)
        self.assertEqual(receiver.gaps,0)

    def test_summary_skips_discontinuous_wheels(self):
        common = dict(bumps_wheeldrops='0',cliff_left='0',cliff_right='0',
                      cliff_front_left='0',cliff_front_right='0',segment='0')
        rows = [dict(common,device_ms='0',encoder_left='65534',encoder_right='10'),
                dict(common,device_ms='15',encoder_left='2',encoder_right='14'),
                dict(common,device_ms='30',encoder_left='500',encoder_right='500',segment='1')]
        summary = summarize(rows)
        self.assertEqual(summary['left_absolute_encoder_counts'],4)
        self.assertEqual(summary['wheel_intervals_skipped'],1)

if __name__ == '__main__':
    unittest.main()
