from enum import Enum
from typing import Optional

from scan.Resource import Resource


class TransportType(str, Enum):
    UNKNOWN = "UNKNOWN"
    Ethernet = "ETH"
    FibreChannel = "FC"
    InfiniBand = "IB"


class PowerState(str, Enum):
    UNKNOWN = "UNKNOWN"
    OFF = "OFF"
    ON = "ON"


class LinkState(str, Enum):
    UNKNOWN = "UNKNOWN"
    DOWN = "DOWN"
    UP = "LINKED"


class Network(Resource):
    type: TransportType = TransportType.UNKNOWN
    power: PowerState = PowerState.UNKNOWN
    link: LinkState = LinkState.UNKNOWN
    mac: Optional[str]
