from enum import Enum
from typing import Optional


class HEALTH_STATUS(str, Enum):
    UNKNOWN = "UNKNOWN"  # default status
    OK = "OK"  # all is good
    DEGRADED = "DEGRADED"  # errors or partially broken
    FAILED = "FAILED"  # device is broken


class Resource:
    id: Optional[str]
    health: HEALTH_STATUS = HEALTH_STATUS.UNKNOWN

    def __init__(self):
        self.health = HEALTH_STATUS.UNKNOWN
