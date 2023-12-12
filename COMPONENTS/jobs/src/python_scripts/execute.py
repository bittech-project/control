from collections import namedtuple
import subprocess
import codecs

ExecResult = namedtuple("ExecResult", "retcode, stdout, stderr")


def executeShell(cmd) -> ExecResult:
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True)
    except Exception as e:
        return ExecResult(1, "", str(e))
    return ExecResult(result.returncode, codecs.decode(result.stdout, 'unicode-escape'), codecs.decode(result.stderr, 'unicode-escape'))
