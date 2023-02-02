import sys

def combine_sources(inputs, output):
  if len(inputs) < 2:
    raise Exception('Too few files to combine')
  for src in inputs:
    print(f'#line 1 "{src}"', file=output)
    with open(src) as f:
      output.writelines(f.readlines())

if __name__ == '__main__':
  combine_sources(sys.argv[1:], sys.stdout)
