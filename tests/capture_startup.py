import os
import pathlib
import subprocess
import sys
build=pathlib.Path(sys.argv[1]).resolve()
env=dict(os.environ,DBUS_SESSION_BUS_ADDRESS='unix:path=/nonexistent/larp-test-bus')
result=subprocess.run([str(build/'larp-host'),'--capture','127.0.0.1','5000','1','5'],env=env,capture_output=True,text=True,timeout=5)
assert result.returncode!=0
assert 'Select a screen' in result.stdout,(result.stdout,result.stderr)
assert result.stderr
result=subprocess.run([str(build/'larp-host'),'--capture','127.0.0.1','5000','1','0'],env=env,capture_output=True,text=True,timeout=5)
assert result.returncode!=0 and 'Select a screen' not in result.stdout
result=subprocess.run([str(build/'larp-capture-probe'),'1','/tmp/larp-no-bus.ppm'],env=env,capture_output=True,text=True,timeout=5)
assert result.returncode!=0 and 'Select a screen' in result.stdout,(result.stdout,result.stderr)
