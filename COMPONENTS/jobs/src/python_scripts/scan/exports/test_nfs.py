import pytest
from scan.exports.nfs import NfsExport
from Fixture import Fixture

GET_EXPORTS_FIXTURE = [
    Fixture({
        "exportfs": """/mnt/testlib/ssss
                <world>
/mnt/testlib/aaaa
                <world>
"""
    }, (['/mnt/testlib/ssss', '/mnt/testlib/aaaa'])
    ),
    Fixture({
        "exportfs": """/mnt/testlib/ssss123_
                <world>
/mnt/testlib/aaaaa__1a
                <world>
"""
    }, (['/mnt/testlib/ssss123_', '/mnt/testlib/aaaaa__1a'])
    )
]

PARSE_FIXTURE = [
    Fixture({
        "exportfs": """/mnt/testlib/ssss""",
        "ss": '''
tcp   LISTEN    0      64                                        0.0.0.0:nfs                        0.0.0.0:*
tcp   ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.222:701
tcp   ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.181:782
tcp   LISTEN    0      64                                           [::]:nfs                           [::]:*
'''}, ({
            'clients': [],
            'health': 'OK',
            'exportPath': '/mnt/testlib/ssss',
            'proto': 'nfs'
        }, [])
    ),
    Fixture({
        "exportfs": """/mnt/testlib/ssss""",
        "ss": '''
LISTEN    0      64                                        0.0.0.0:nfs                        0.0.0.0:*
ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.222:701
ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.181:782
LISTEN    0      64                                           [::]:nfs                           [::]:*
'''}, ({
            'clients': [],
            'health': 'OK',
            'exportPath': '/mnt/testlib/ssss',
            'proto': 'nfs'
        }, [])
    ),
    Fixture({
        "exportfs": """/mnt/testlib/sss123_""",
        "ss": '''
LISTEN    0      64                                        0.0.0.0:nfs                        0.0.0.0:*
ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.222:701
ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.181:782
LISTEN    0      64                                           [::]:nfs                           [::]:*
'''}, ({
            'clients': [],
            'health': 'OK',
            'exportPath': '/mnt/testlib/sss123_',
            'proto': 'nfs'
        }, [])
    ),
    Fixture({
        "exportfs": """/mnt/testlib/sss123_""",
        "ss": '''
ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.222:701
ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.181:782
'''}, ({
            'clients': [],
            'health': 'UNKNOWN',
            'exportPath': '/mnt/testlib/sss123_',
            'proto': 'nfs'
        }, [])
    )
]


@pytest.mark.parametrize("f", GET_EXPORTS_FIXTURE)
def test__getExports(f: Fixture):
    nfsExport = NfsExport.getExports(f.input["exportfs"])
    # print(nfsExport)
    if not f.expected:
        assert nfsExport is None
        return
    for name in f.expected:
        assert name in nfsExport


@pytest.mark.parametrize("f", PARSE_FIXTURE)
def test__parse(f: Fixture):
    nfsExport = NfsExport.parse(f.input["exportfs"], f.input["ss"])
    if not f.expected:
        assert nfsExport is None
        return
    assert nfsExport.__dict__ == f.expected[0]
