import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('xiao_capture',Path(__file__).resolve().parents[1]/'tools/xiao_capture.py')
m=importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

def frame(seq,ms,ma=-1000,**kwargs):
    return dict(sequence=seq,ms=ms,generation=seq,rejected_frames=0,source_lost=0,
                sensors={'22':15000,'23':ma},**kwargs)

def batch(rows,**kwargs):
    return dict(schema=1,boot_id='a'*16,session=1,records=rows,
                next=rows[-1]['sequence'] if rows else 0,**kwargs)

class CaptureTests(unittest.TestCase):
    def test_energy_discharge_charge_and_crossing(self):
        e=m.Energy()
        self.assertEqual(e.add(frame(1,0)) ,15)
        for n in range(1,11): e.add(frame(n+1,n*100))
        self.assertAlmostEqual(e.discharge_wh,15/3600)
        self.assertEqual(e.charge_wh,0)
        e=m.Energy(); e.add(frame(1,0,-1000)); e.add(frame(2,100,1000))
        self.assertAlmostEqual(e.discharge_wh,15*.025/3600)
        self.assertAlmostEqual(e.charge_wh,15*.025/3600)

    def test_gaps_rejections_missing_and_clock_rollover(self):
        for changed in ('sequence','rejected_frames','source_lost','ms'):
            e=m.Energy(); e.add(frame(1,0)); row=frame(2,100)
            row[changed]+=500; e.add(row)
            self.assertEqual(e.observed_s,0)
        e=m.Energy(); e.add(frame(1,0xfffffff0)); e.add(frame(2,84))
        self.assertAlmostEqual(e.observed_s,.1)
        row=frame(3,184); row['sensors'].pop('23')
        self.assertIsNone(e.add(row)); self.assertEqual(e.observed_s,.1)

    def test_capture_preserves_missing_and_detects_loss_and_identity(self):
        with tempfile.TemporaryDirectory() as td:
            writer=m.CaptureWriter(Path(td)/'capture')
            writer.accept(batch([frame(1,0),frame(3,100)]))
            self.assertEqual(writer.lost,1)
            self.assertEqual(writer.energy.observed_s,0)
            changed=batch([frame(4,200)]); changed['session']=2
            with self.assertRaises(ValueError): writer.accept(changed)
            writer.close('complete')
            summary=json.loads((Path(td)/'capture/session.json').read_text())
            self.assertEqual(summary['lost_records'],1)
            self.assertFalse(summary['energy']['hardware_verified'])
            import csv
            with (Path(td)/'capture/sensors.csv').open() as f:
                row=next(csv.DictReader(f))
            self.assertEqual(row['MainBrushCurrent'],'')
            self.assertEqual(row['VoltageMv'],'15000')

    def test_manifest_wrong_target_oversize_or_changed_image(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'app.bin'; data=bytearray(32); data[0]=0xe9; data[12]=9
            p.write_bytes(data); manifest=m.firmware_manifest(p)
            self.assertEqual(manifest['bytes'],32)
            data[12]=0; p.write_bytes(data)
            with self.assertRaises(ValueError): m.firmware_manifest(p)

    def test_record_command_lifecycle_and_final_drain(self):
        from contextlib import redirect_stdout
        from io import StringIO
        class FakeClient:
            def __init__(self):
                self.session=0; self.running=False; self.commands=[]; self.rows=[]
            def command(self,action,**args):
                self.commands.append((action,args))
                if action=='record-start':
                    self.session+=1; self.running=True; self.rows=[frame(1,0),frame(2,100)]
                if action=='record-stop':
                    self.running=False; self.rows.append(frame(3,200))
            def recording(self,after=0,identity=None):
                rows=[r for r in self.rows if r['sequence']>after]
                return dict(schema=1,boot_id='a'*16,session=self.session,
                    recording=self.running,records=rows,next=rows[-1]['sequence'] if rows else after,
                    has_more=False)
        client=FakeClient()
        with tempfile.TemporaryDirectory() as td, redirect_stdout(StringIO()):
            output=Path(td)/'capture'
            m.record(client,'base',output,.001)
            summary=json.loads((output/'session.json').read_text())
            self.assertEqual(summary['frames'],3)
            self.assertEqual(summary['result'],'complete')
            self.assertEqual(summary['lost_records'],0)
            self.assertEqual(client.commands,[('record-start',{'profile':'base'}),('record-stop',{})])
            self.assertFalse(client.running)

    def test_fetch_stopped_capture_without_restarting_and_reports_overwrite(self):
        from contextlib import redirect_stdout
        from io import StringIO
        class Client:
            def __init__(self): self.commands=[]
            def command(self, action, **args): self.commands.append(action)
            def recording(self, after=0, identity=None):
                rows=[frame(20,0),frame(21,100)] if not after else []
                return dict(schema=1,boot_id='a'*16,session=1,recording=False,
                    records=rows,next=21,has_more=False)
        client=Client()
        with tempfile.TemporaryDirectory() as td, redirect_stdout(StringIO()):
            output=Path(td)/'capture'; m.record(client,None,output,None,attach=True)
            summary=json.loads((output/'session.json').read_text())
            self.assertEqual(summary['frames'],2)
            self.assertEqual(summary['lost_records'],19)
            self.assertNotIn('record-start',client.commands)

    def test_failed_start_cancels_pending_capture_and_keeps_partial_result(self):
        class RebootClient:
            def __init__(self): self.commands=[]
            def command(self,action,**args): self.commands.append(action)
            def recording(self):
                return dict(schema=1,boot_id='a' if not self.commands else 'b',
                            session=0,recording=False,records=[],next=0)
        client=RebootClient()
        with tempfile.TemporaryDirectory() as td:
            output=Path(td)/'capture'
            with self.assertRaises(RuntimeError): m.record(client,'base',output,1)
            self.assertEqual(client.commands,['record-start','record-stop'])
            self.assertEqual(json.loads((output/'session.json').read_text())['result'],'failed')

    def test_redirect_protection(self):
        from http.server import BaseHTTPRequestHandler,HTTPServer
        from threading import Thread
        import urllib.error
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                self.send_response(302); self.send_header('Location','http://127.0.0.1:1/secret'); self.end_headers()
            def log_message(self,*args): pass
        server=HTTPServer(('127.0.0.1',0),Handler)
        thread=Thread(target=server.serve_forever,daemon=True); thread.start()
        try:
            with self.assertRaises(urllib.error.HTTPError):
                m.Client(f'http://127.0.0.1:{server.server_port}','test-token').call('/status')
        finally: server.shutdown(); server.server_close(); thread.join()

if __name__=='__main__': unittest.main()
