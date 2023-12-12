from typing import List
from itertools import groupby
import re

from scan.networks.ethernet import EthResource
from execute import executeShell


class NetDevInfo:
    id: str = ""
    fullName: str = ""
    pciSlot: str = ""
    numaId: str = ""

    ports: List[EthResource] = []

    @staticmethod
    def _grouper(item):
        reg = re.compile(r"(\d*)\:\d*\.\d+$")
        regResult = reg.findall(item.deviceInfo['pciSlot'])
        pciGroupId = regResult[0] if regResult else ""
        return pciGroupId

    @staticmethod
    def _scan() -> List["NetDevInfo"]:
        result: List[NetDevInfo] = []
        ungrouped_eths: List[EthResource] = EthResource._startParse(
            executeShell("ip -j a").stdout)
        ungrouped_eths = sorted(ungrouped_eths, key=NetDevInfo._grouper)
        groups = groupby(ungrouped_eths, NetDevInfo._grouper)
        groupedEths = [[item.__dict__ for item in data]
                       for (key, data) in groups]
        for group in groupedEths:
            netInst = NetDevInfo()
            netInst.id = "netdev" + \
                group[0]['deviceInfo']['pciSlot'].replace(
                    ":", "").replace(".", "")
            netInst.fullName = group[0]['deviceInfo']['fullName']
            netInst.pciSlot = group[0]['deviceInfo']['pciSlot']
            netInst.numaId = group[0]['deviceInfo']['numaId']
            for entry in group:
                # removing deviceInfo field after move data
                del entry['deviceInfo']
            netInst.ports = group
            result.append(netInst)
        return result


def scan() -> List[NetDevInfo]:
    result: List[NetDevInfo] = []
    result = NetDevInfo._scan()

    return result
