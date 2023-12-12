#!/usr/bin/env python3
from typing import List, Union
from enum import Enum
from fileSystemUtils import readFile
from execute import executeShell

import re
import json

MAC_LABEL = "address"
STATUS_LABEL = "operstate"
DEVICENAME_LABEL = "ifname"
ADDR_IP_LABEL = "local"
ADDR_PREFIX_LABEL = "prefixlen"


class LINK_STATUS(Enum):
    UNKNOWN = "UNKNOWN"
    LINKED = "LINKED"
    NOCARRIER = "NO-CARRIER"


class NETWORK_CAPABILITY(Enum):
    UNKNOWN = "UNKNOWN"
    ETHERNET = "ETHERNET"
    RDMA = "RDMA"


class POWER_STATUS(Enum):
    UNKNOWN = "UNKNOWN"
    UP = "UP"
    DOWN = "DOWN"


class Info:
    fullName: str = ""
    pciSlot: str = ""
    numaId: str = ""


class EthResource:
    id: str = ""
    mac: str = ""
    name: str = ""
    addresses: List[str] = list()
    # currentLinkSpeed: str # TODO
    maxLinkSpeedMbps: str = ""
    status: Union[LINK_STATUS, str] = LINK_STATUS.UNKNOWN
    power: Union[POWER_STATUS, str] = POWER_STATUS.UNKNOWN

    info: Info = Info()

    @staticmethod
    def _startParse(ipRaw: str) -> List["EthResource"]:
        result: List[EthResource] = []

        try:
            ipJsonObject = json.loads(ipRaw)
        except Exception:
            return []

        for entry in ipJsonObject:
            resource = EthResource._parse(entry)
            if resource:
                result.append(resource)

        return result

    @staticmethod
    def _findMaxLinkSpeed(deviceName: str) -> str:
        linkSpeedPath = readFile(f"/sys/class/net/{deviceName}/speed")
        if linkSpeedPath.retcode == 1:
            return ""
        linkSpeedFileContent = linkSpeedPath.stdout.strip()
        return "" if (linkSpeedFileContent == "-1") else linkSpeedFileContent

    @staticmethod
    def _findNumaId(deviceName: str) -> str:
        numaId = executeShell(
            f"cat /sys/class/net/{deviceName}/device/numa_node").stdout.strip()

        return numaId if numaId else ""

    @staticmethod
    def _findPciSlot(deviceName: str) -> str:
        pciSlot = executeShell(
            f"grep PCI_SLOT_NAME /sys/class/net/{deviceName}/device/uevent")
        pciSlot = pciSlot.stdout.replace(
            "PCI_SLOT_NAME=", "").strip().replace('0000:', '')

        return pciSlot if pciSlot else ""

    @staticmethod
    def _findDeviceFullName(pciSlot: str) -> str:
        FULLNAME_REGEX = re.compile(r"\w+ controller\: ([\w \[\-\]]+.*$)")

        fullNamePciOut = executeShell(f"lspci -D | grep {pciSlot}")
        fullNameDevice = FULLNAME_REGEX.findall(fullNamePciOut.stdout.strip())

        return fullNameDevice[0] if fullNameDevice else ""

    @staticmethod
    def _findDeviceInfo(deviceName: str) -> Info:
        result = Info()

        result.numaId = EthResource._findNumaId(deviceName)

        result.pciSlot = EthResource._findPciSlot(deviceName)
        result.fullName = EthResource._findDeviceFullName(result.pciSlot)

        return result

    # arguments maxLinkSpeed, deviceInfo for test purposes only
    @staticmethod
    def _parse(rawIpEntry: str, maxLinkSpeed: str = "", deviceInfo: Info = None) -> "EthResource":
        if len(rawIpEntry) <= 0:
            return []

        if rawIpEntry.get("link_type") == "loopback":
            return  # filter localhost loopback

        ethernetResource = EthResource()
        ethernetResource.name = rawIpEntry.get(DEVICENAME_LABEL)
        ethernetResource.mac = rawIpEntry.get(MAC_LABEL)
        ethernetResource.power = POWER_STATUS.UP.value if (
            rawIpEntry.get(STATUS_LABEL) == "UP") else POWER_STATUS.DOWN.value

        ethernetResource.status = LINK_STATUS.NOCARRIER.value
        for entry in rawIpEntry.get("flags"):
            if entry == "UP":
                ethernetResource.status = LINK_STATUS.LINKED.value
                break

        addrArray = []
        for addrEntry in rawIpEntry.get("addr_info"):
            if addrEntry:
                addrArray.append(addrEntry.get(ADDR_IP_LABEL) +
                                 "/" + str(addrEntry.get(ADDR_PREFIX_LABEL)))

        ethernetResource.addresses = addrArray
        ethernetResource.id = ethernetResource.name + \
            ethernetResource.mac.replace(":", "")
        # UNCORRECT_SYMBOLS_REGEX = re.compile(r"[\w-]+")

        ethernetResource.maxLinkSpeedMbps = EthResource._findMaxLinkSpeed(
            ethernetResource.name) if maxLinkSpeed == "" else maxLinkSpeed

        if deviceInfo == None:
            deviceInfoInst = Info()
            deviceInfoInst = EthResource._findDeviceInfo(ethernetResource.name)
            ethernetResource.deviceInfo = deviceInfoInst.__dict__
        else:
            ethernetResource.deviceInfo = deviceInfo.__dict__

        return ethernetResource


def scanEthernet() -> List[EthResource]:
    result: List[EthResource] = []
    result = EthResource._startParse(executeShell("ip -j a").stdout)

    return result
