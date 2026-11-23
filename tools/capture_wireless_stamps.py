"""Record r31 timing frames from the Lite 2 physical evdev node in RAW mode."""
import argparse
from pathlib import Path
import re
import subprocess
import time

from analyze_wireless_stamps import EVENT, analyze, read_reports
import json

def capture(output, seconds):
    listing=subprocess.check_output(['adb','-d','shell','getevent','-p'],text=True,encoding='utf-8')
    node=re.search(r'(/dev/input/event\d+)\s+name:\s+"Lite2 Air Mouse Plus Mouse"',listing)
    if node is None:raise SystemExit('Lite 2 S Bluetooth mouse not connected over the Y700')
    start=None
    with output.open('xb') as trace:
        with subprocess.Popen(['adb','-d','shell','timeout',str(seconds+90),'getevent','-t',node[1]],
                              stdout=subprocess.PIPE) as reader:
            try:
                print('Waiting for timed reports. In RAW outside the monitor press + and - together.',flush=True)
                for line in reader.stdout:
                    event=EVENT.search(line.decode('ascii',errors='ignore'))
                    if start is None:
                        if not event or (event[2],event[3]) != ('0002','0002') or not int(event[4],16)&0x8000:
                            continue
                        start=time.monotonic()
                    trace.write(line)
                    if time.monotonic()-start>=seconds:break
            finally:
                reader.terminate()
    if start is None:raise SystemExit('No timing markers received')
    return analyze(read_reports(output))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--seconds',type=int,default=60,choices=range(1,121),metavar='1..120')
    args=parser.parse_args()
    print(json.dumps(capture(args.output,args.seconds),indent=2))

