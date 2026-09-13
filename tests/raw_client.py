import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import zlib

build=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temporary:
    path=pathlib.Path(temporary)/'capture.ppm'
    client=subprocess.Popen([str(build/'larp-client'),'--raw','127.0.0.1','0','1',str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        line=client.stdout.readline()
        assert line.startswith('Listening: '),(line,client.communicate())
        pixels=bytes(range(12))
        prefix=struct.pack('!IHHII',0x4c524742,1,1,2,2)
        raw=prefix+struct.pack('!I',zlib.crc32(prefix+pixels))+pixels
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
            for frame,body in [(1,raw),(2,raw[:-1]+b'\xff')]:
                packet=struct.pack('!IHQIIQI',0x4c415250,1,frame,0,1,0,len(body))+body
                sender.sendto(packet,('127.0.0.1',int(line.split()[-1])))
        out,err=client.communicate(timeout=3)
        assert client.returncode==0,(out,err)
        assert 'Validated: 1 ' in out and 'Corrupt: 1 ' in out,out
        assert path.read_bytes()==b'P6\n2 2\n255\n'+pixels
    finally:
        if client.poll() is None:client.terminate();client.wait()
