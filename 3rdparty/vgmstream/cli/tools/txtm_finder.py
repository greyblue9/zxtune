import os
import struct

mpfTable = {}

for fname in os.listdir(os.getcwd()):
    if fname.lower()[-4:] not in [".map",".mpf"]:
        continue

    print(fname)
    with open(fname,"rb") as f:
        magic=f.read(4)
        if magic!=b"PFDx":
            raise Exception(f"Wrong header magic in {f.name}")
        version,introNode,numNodes,numSections,b1,b2,b3,numEvents=struct.unpack("8B",f.read(8))
        if version>1: raise Exception("Unsupported version %d in %s" % (version,f.name))
        f.seek(numNodes*0x1c,1)
        f.seek(numEvents*numSections,1)
        samplesTable=f.tell()

        for musName in os.listdir(os.getcwd()):
            if musName.lower()[-4:] not in [".mus",".trm",".trj"]:
                continue

            f.seek(samplesTable)
            with open(musName,"rb") as f2:
                passed=True
                for _ in range(numNodes):
                    offset=struct.unpack(">I",f.read(4))[0]
                    f2.seek(offset)
                    magic=f2.read(4)
                    if magic!=b"SCHl":
                        passed=False
                        break

            if passed:
                mpfTable[fname]=musName
                break

with open(".txtm","w") as f:
    for mpf, value in mpfTable.items():
        f.write("%s:%s\n" % (mpf, value))
