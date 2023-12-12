#!/usr/bin/env python3
from scan.exports.Export import Export, ExportsType
from fileSystemUtils import readFile, lsInArr
from typing import Optional, List, Dict
from scan.Resource import HEALTH_STATUS
from execute import executeShell

import re

NVMET = "/sys/kernel/config/nvmet"
NVMET_SUBSYSTEMS = f"{NVMET}/subsystems"
NVMET_PORT = f"{NVMET}/ports"
TRANSPORT_PROTOCOL = "addr_trtype"
IP_ADDRESS = "addr_traddr"
PORT = "addr_trsvcid"


class NvmeofExport(Export):
    def __init__(self):
        super().__init__()
        self.proto = ExportsType.NVME

    @staticmethod
    def getNvmeofExports() -> Optional[List["Export"]]:
        result: List[Export] = []
        for directory in NvmeofExport._getNVMEtPorts():
            rawNvme = NvmeofExport._parse(directory)
            if rawNvme:
                result.append(rawNvme)
        return result

    @staticmethod
    def _getNVMEtPorts() -> Optional[List[str]]:
        nvmet_ports = lsInArr(NVMET_PORT)
        if nvmet_ports.retcode == 1:
            raise nvmet_ports.stderr

        return nvmet_ports.stdout

    @staticmethod
    def _parse(directory: str) -> Optional["NvmeofExport"]:
        # directory this is /sys/kernel/config/nvmet/subsystems/{directory}
        if not directory:
            return None
        rawNVME = NvmeofExport()
        rawNVME._setTargetName(directory)
        rawNVME._setPort(directory)
        rawNVME._setIsEnable()
        rawNVME._setInitiatorList()
        if not rawNVME.health:
            return None
        return rawNVME

    def _setTargetName(self, directory):
        name = lsInArr(f"{NVMET_PORT}/{directory}/subsystems")
        if name.retcode == 1:
            return
        try:
            self.exportPath = name.stdout[0]
        except IndexError:
            return

    def _setPort(self, directory):
        # /sys/kernel/config/nvmet/ports/{directory}/{param}
        nvmetPort = readFile(f'{NVMET_PORT}/{directory}/{PORT}')
        if nvmetPort.retcode == 1:
            return None
        self.port = nvmetPort.stdout.strip()

    def _setIsEnable(self):
        status = readFile(
            f"{NVMET_SUBSYSTEMS}/{self.exportPath}/namespaces/1/enable")
        if status.retcode == 1:
            return None
        self.health = HEALTH_STATUS.OK if int(
            status.stdout) == 1 else HEALTH_STATUS.DEGRADED

    def _setInitiatorList(self):
        initiators: List[str] = []

        socketConnections = executeShell(f"ss -tla | grep {self.port}")

        RAID_PATH_REGEX = re.compile(
            rf"^ESTAB.*:{self.port}\s*(\S*):\d+\s*$", re.MULTILINE)
        initiators = list(dict.fromkeys(
            RAID_PATH_REGEX.findall(socketConnections.stdout)))
        if initiators:
            self.clients = initiators


def startParsing(rawNvmeofExports: Optional[List[Export]]) -> List[Export]:
    result: List[NvmeofExport] = []
    if not rawNvmeofExports:
        return []
    for value in rawNvmeofExports:
        if value:
            result.append(value)
    return result


def scanNvmeof() -> List[Dict]:
    rawNvmeofExports: Optional[List[Export]] = NvmeofExport.getNvmeofExports()
    return startParsing(rawNvmeofExports)
