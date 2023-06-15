#!/usr/bin/python3

import re
import subprocess

def getDeps(target):
  print(f'Targets for {target}')
  proc = subprocess.run(['./gradlew', target, '--dry-run'], stdout=subprocess.PIPE)
  return {
      line[8:]
      for line in proc.stdout.decode('utf-8').split()
      if line.startswith(':zxtune:')
  }

def checkDeps(descr):
  deps = getDeps(descr['target'])
  print(' Missing:')
  for req in descr['required']:
    found = any(re.search(req, d) for d in deps)
    if not found:
      print(f'  {req}')
  print(' Redundand:')
  for req in descr['excluded']:
    for d in deps:
      if re.search(req, d):
        print(f'  {d}')

DEPS = [
    {
      'target': 'publicBuildFatRelease',
      'required': [ 'publishApk.*', 'assemble.*' ],
      'excluded': [ 'publishAab*.', 'bundle.*' ]
    },
    {
      'target': 'publicBuildWithCrashlytics',
      'required': [ 'publishApkFatRelease', 'publishAabThinRelease', 'publishPdb', 'prepareDebugSymbols.*'],
      'excluded': [ '.*bundleFat.*', '.*assembleThin.*']
    }
]

for d in DEPS:
  checkDeps(d)
