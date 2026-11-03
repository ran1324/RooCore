import csv
import json
import socket
import sys
import tempfile
import threading
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'server'))
from recorder import Run, serve_connection
from oi import CaptureDecoder
from test_recorder import frame

TOKEN='a'*48

def record(seq,data,**extra):
    return dict(type='serial',boot=1,capture=1,seq=seq,ms=seq*15,
                overruns=0,dropped=0,hex=data.hex(),**extra)

class CaptureTests(unittest.TestCase):
    def test_drop_and_status_clear_partial_frame(self):
        d=CaptureDecoder(); raw=frame([7,1])
        d.feed(record(0,raw[:3]))
        d.feed(dict(type='status',boot=1,capture=1,ms=10,overruns=0,dropped=3))
        msg=record(2,raw[3:]+raw);msg['dropped']=3
        result=d.feed(msg)
        self.assertEqual(len(result),1)
        self.assertEqual(d.missing_records,1)
        self.assertGreater(result[0]['segment'],1)

    def test_boot_change_and_malformed_data(self):
        d=CaptureDecoder(); raw=frame([43,0,1,44,0,2])
        first=d.feed(record(0,raw))[0]
        second=record(0,raw);second['boot']=2
        self.assertGreater(d.feed(second)[0]['segment'],first['segment'])
        for value in [True,-1,2**32]:
            invalid=record(1,raw);invalid['seq']=value
            with self.assertRaises(ValueError): d.feed(invalid)
        invalid=record(1,raw);invalid['hex']='00 0'
        with self.assertRaises(ValueError): d.feed(invalid)

class ServerTests(unittest.TestCase):
    def connect(self,run,token=TOKEN):
        a,b=socket.socketpair()
        b.settimeout(2)
        thread=threading.Thread(target=serve_connection,args=(a,('127.0.0.1',1),run,TOKEN,lambda:False))
        thread.start()
        hello=json.dumps(dict(type='hello',protocol=2,device='roocore-123abc',boot=1,token=token)).encode()+b'\n'
        # Deliberately split the handshake across socket writes.
        b.sendall(hello[:13]);b.sendall(hello[13:])
        return b,thread

    def test_record_reconnect_reboot_and_redact_token(self):
        with tempfile.TemporaryDirectory() as folder:
            run=Run(Path(folder)/'run','wheels')
            raw=frame([7,1,23,255,156,43,255,254,44,0,2])
            for boot in (1,2):
                sock,thread=self.connect(run)
                data=b''
                while b'START wheels\n' not in data: data+=sock.recv(100)
                self.assertTrue(data.startswith(b'READY\n'))
                for seq,chunk in enumerate((raw[:5],raw[5:])):
                    msg=record(seq,chunk);msg['boot']=boot;msg['token']='must-not-be-logged'
                    sock.sendall(json.dumps(msg).encode()+b'\n')
                sock.close();thread.join(3);self.assertFalse(thread.is_alive())
            run.close()
            with (run.directory/'sensors.csv').open() as f: rows=list(csv.DictReader(f))
            self.assertEqual(len(rows),2)
            self.assertEqual(rows[0]['current_ma'],'-100')
            self.assertNotEqual(rows[0]['segment'],rows[1]['segment'])
            self.assertEqual(run.metadata['connections'],2)
            for file in run.directory.iterdir():
                text=file.read_text(); self.assertNotIn(TOKEN,text);self.assertNotIn('must-not-be-logged',text)

    def test_bad_token_never_starts_robot_stream(self):
        with tempfile.TemporaryDirectory() as folder:
            run=Run(Path(folder)/'run','basic')
            sock,thread=self.connect(run,'b'*48)
            self.assertEqual(sock.recv(100),b'')
            sock.close();thread.join(3);run.close()
            self.assertEqual(run.metadata['connections'],0)
            self.assertIn('authentication_failed',(run.directory/'events.jsonl').read_text())

if __name__=='__main__': unittest.main()
