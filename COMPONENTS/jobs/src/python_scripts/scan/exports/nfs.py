#!/usr/bin/env python3
from scan.exports.Export import Export, ExportsType
from execute import executeShell
from typing import Optional, List
from scan.Resource import HEALTH_STATUS
import re

DEFAULT_TESTLIB_EXPORT_PATH = "/mnt/testlib"


class NfsExport(Export):
    def __init__(self):
        super().__init__()
        self.proto = ExportsType.NFS

    @staticmethod
    def getExports(exportfs_out: str) -> List[str]:
        '''
        /mnt/testlib/tb_59vqhg  *(async,wdelay,hide,no_subtree_check,fsid=555877ac-98c1-4354-ad68-c97200521cf8,sec=sys,rw,secure,no_root_squash,no_all_squash)
        '''

        list_of_exports: List[str]
        REGEX = re.compile(
            rf"({DEFAULT_TESTLIB_EXPORT_PATH}\S+)\s+", re.MULTILINE)

        list_of_exports = REGEX.findall(exportfs_out)
        if not list_of_exports:
            return []
        list_of_exports = list(set(list_of_exports))

        return list_of_exports

    @staticmethod
    def parse(exportFS: str, socketConnectionsRaw: str) -> Optional["NfsExport"]:
        if NfsExport._skip(exportFS):
            return None
        nfsInst = NfsExport()
        nfsInst.exportPath = exportFS
        # nfsInst._setClients(socketConnectionsRaw)
        if nfsInst._validate():
            return None
        # TODO: Check health of nfs share by socket, etc.
        nfsInst._setHealth(socketConnectionsRaw)
        return nfsInst

    def _validate(self) -> bool:
        return not self.exportPath

    @staticmethod
    def _skip(exportFS: str) -> bool:
        return not exportFS

    def _setHealth(self, socketConnections: str):
        socket: List[str] = []

        REGEX = re.compile(r"LISTEN.*:nfs\s*(\S*):.*$", re.MULTILINE)
        socket = list(dict.fromkeys(REGEX.findall(socketConnections)))
        if socket:
            self.health = HEALTH_STATUS.OK
        else:
            self.health = HEALTH_STATUS.UNKNOWN


# ss -a | grep nfs
'''
tcp   LISTEN    0      64                                        0.0.0.0:nfs                        0.0.0.0:*
tcp   ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.222:701
tcp   ESTAB     0      0                                   192.168.84.44:nfs                 192.168.83.181:782
tcp   LISTEN    0      64                                           [::]:nfs                           [::]:*
'''

# cat /proc/fs/nfsd/clients/7/info
'''
clientid: 0xed209a7064d0db6f
address: "192.168.83.181:782"
name: "Linux NFSv4.2 kma"
minor version: 2
Implementation domain: "kernel.org"
Implementation name: "Linux 5.15.0-78-generic #85-Ubuntu SMP Fri Jul 7 15:25:09 UTC 2023 x86_64"
Implementation time: [0, 0]
'''


def scanNfs() -> List[NfsExport]:
    try:
        result: List[NfsExport] = []
        exportfs_out: str = executeShell("exportfs -s")
        if not exportfs_out:
            return []

        list_of_exports = NfsExport.getExports(exportfs_out.stdout)

        # TODO: scan EXCHANGE_ID and SETCLIENT_ID traffic for map clients by volumes
        socketConnections = executeShell("ss -tla | grep nfs")
        if socketConnections:
            socketConnections = socketConnections.stdout

        for export in list_of_exports:
            exportObj = NfsExport.parse(export, socketConnections)
            if not exportObj:
                continue
            result.append(exportObj)
        return result
    except Exception:
        return []
