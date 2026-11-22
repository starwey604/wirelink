#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile actual DWC2 completion functions against mocked register/buffer I/O."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zephyr-base',type=Path,default=Path('/home/ww/zephyrproject/zephyr'))
    p.add_argument('--source',type=Path,help='explicit driver snapshot, e.g. a negative control')
    p.add_argument('--case',choices=('in','out'))
    args=p.parse_args();source=args.source or args.zephyr_base/'drivers/usb/udc/udc_dwc2.c'
    text=source.read_text();functions=[]
    for signature in ('static int dwc2_handle_evt_din(', 'static inline void dwc2_handle_out_xfercompl('):
        start=text.index(signature);end=text.index('\n}\n',start)+3;functions.append(text[start:end])
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary);(directory/'dwc2_functions.inc').write_text('\n'.join(functions))
        fixture=Path(__file__).with_name('zephyr_dwc2_completion_fixture.c');binary=directory/'completion'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(directory),str(fixture),'-o',str(binary)],check=True)
        return subprocess.run([str(binary)]+([args.case] if args.case else [])).returncode
if __name__=='__main__':raise SystemExit(main())
