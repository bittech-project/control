from collections import namedtuple
from pathlib import Path
import os

FileResult = namedtuple("FileResult", "retcode, stdout, stderr")


def readFile(path: str, mode: str = "r") -> FileResult:
    try:
        result = open(path, mode).read()
    except Exception as e:
        return FileResult(1, "", str(e))
    return FileResult(0, result, "")

def lsDirInArr(path) -> FileResult:
    try:
        result = [Path(d).name for d in 
                  (os.path.join(path, d1) for d1 in os.listdir(path))
                     if os.path.isdir(d)]
    except Exception as e:
        return FileResult(1, "", str(e))
    return FileResult(0, result, "")

def lsInArr(path) -> FileResult:
    try:
        result = os.listdir(path)
    except Exception as e:
        return FileResult(1, "", str(e))
    return FileResult(0, result, "")
