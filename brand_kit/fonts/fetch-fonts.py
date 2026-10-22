#!/usr/bin/env python3
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
"""Download unmodified brand fonts and their licenses from Google Fonts' official repository.
Run with Python 3 on a machine with Internet access. No third-party packages required.
"""
from pathlib import Path
from urllib.request import urlopen, Request
from urllib.parse import quote
import sys
ROOT=Path(__file__).resolve().parent
BASE='https://raw.githubusercontent.com/google/fonts/main/ofl/'
FAMILIES={
 'ibmplexmono': ['IBMPlexMono-Regular.ttf','IBMPlexMono-Medium.ttf','IBMPlexMono-SemiBold.ttf','IBMPlexMono-Bold.ttf','IBMPlexMono-Italic.ttf'],
 'spacegrotesk': ['SpaceGrotesk[wght].ttf'],
 'ibmplexserif': ['IBMPlexSerif-Regular.ttf','IBMPlexSerif-SemiBold.ttf','IBMPlexSerif-Italic.ttf'],
}
failures=[]
for family,files in FAMILIES.items():
 folder=ROOT/family;folder.mkdir(exist_ok=True)
 # License first. Do not distribute a downloaded font without its OFL.
 try:
  req=Request(BASE+family+'/OFL.txt',headers={'User-Agent':'BAMF-brand-font-setup/1.0'})
  with urlopen(req,timeout=30) as response:license_bytes=response.read()
  if b'OPEN FONT LICENSE' not in license_bytes.upper():raise ValueError('License verification failed')
  (folder/'OFL.txt').write_bytes(license_bytes)
 except Exception as exc:
  failures.append(f'{family}/OFL.txt: {exc}');continue
 for filename in files:
  try:
   req=Request(BASE+family+'/'+quote(filename),headers={'User-Agent':'BAMF-brand-font-setup/1.0'})
   with urlopen(req,timeout=30) as response:data=response.read()
   if data[:4] not in (b'\x00\x01\x00\x00',b'OTTO',b'ttcf'):raise ValueError('Response is not a font file')
   (folder/filename).write_bytes(data)
   print('Downloaded',family+'/'+filename)
  except Exception as exc:failures.append(f'{family}/{filename}: {exc}')
if failures:
 print('\nSome downloads failed. Official alternatives are in README.md.',file=sys.stderr)
 for failure in failures:print(failure,file=sys.stderr)
 sys.exit(1)
print('\nDone. Install the .ttf files for desktop use, or load fonts.css for web use.')
