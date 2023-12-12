from scan.exports.Export import Export, ExportsType
from fileSystemUtils import readFile, lsDirInArr
from typing import Optional, List
from scan.Resource import HEALTH_STATUS

SCST_BASE_DIR = "/sys/kernel/scst_tgt/targets/iscsi/"


class ScstExports(Export):
    def __init__(self):
        super().__init__()
        self.proto = ExportsType.ISCSI

    @staticmethod
    def getScstExports() -> Optional[List["Export"]]:
        result: List[Export] = []
        for directory in ScstExports._getTargetNames():
            row = ScstExports._parse(directory)
            if row:
                result.append(row)
        return result

    @staticmethod
    def _getTargetNames() -> List[str]:
        scsiNames = lsDirInArr(SCST_BASE_DIR)
        if scsiNames.retcode == 1:
            return []
        return scsiNames.stdout

    @staticmethod
    def _parse(directory: str) -> Optional["ScstExports"]:
        # directory this is /sys/kernel/scst_tgt/targets/iscsi/{directory}
        if not directory:
            return None
        rowSCST = ScstExports()
        rowSCST._setTargetName(directory)
        # rowSCST._setDevicePath(directory)
        rowSCST._setIsEnable(directory)
        rowSCST._setInitiatorList(directory)
        # rowSCST._setId(directory)
        if not rowSCST.health:
            return None
        return rowSCST

    def _setTargetName(self, directory):
        self.exportPath = directory

    # def _setDevicePath(self, directory):
    #     GET_PATH_REGEX = re.compile(r"^/dev\/.*")
    #     devicePath = readFile(
    #         f"{SCST_BASE_DIR}{directory}/luns/0/device/filename")
    #     if devicePath.retcode == 1:
    #         return
    #     devicePath = GET_PATH_REGEX.findall(devicePath.stdout)[0]
    #     self.devicePath = devicePath

    def _setIsEnable(self, directory):
        status = readFile(f"{SCST_BASE_DIR}{directory}/enabled")
        if status.retcode == 1:
            return
        try:
            self.health = HEALTH_STATUS.OK if int(
                status.stdout) == 1 else HEALTH_STATUS.DEGRADED
        except:
            return

    def _setInitiatorList(self, directory):
        clients = lsDirInArr(f"{SCST_BASE_DIR}{directory}/sessions")
        if clients.retcode == 1:
            return
        self.clients = clients.stdout

    # def _setId(self, directory):
    #     device = lsDirInArr(
    #         f"{SCST_BASE_DIR}{directory}/luns/0/device/handler")
    #     if device.retcode == 1:
    #         return
    #     self.id = device.stdout[0]


def startParsing(rowScstExports: Optional[List[Export]]) -> List[Export]:
    result: List[Export] = []
    if not rowScstExports:
        return []
    for value in rowScstExports:
        if value:
            result.append(value)
    return result


def scanScst() -> List[Export]:
    rawScstExports: Optional[List[Export]
                             ] = ScstExports.getScstExports()
    return startParsing(rawScstExports)
