#!/usr/bin/env python3
from enum import Enum
from typing import List
from scan.Resource import Resource


class ExportsType(str, Enum):
    NFS = "nfs"
    NVME = "nvmeof"
    ISCSI = "iscsi"


class Export(Resource):
    proto: ExportsType
    exportPath: str  # target name for scst
    clients: List[str]
    port: str

    def __init__(self):
        super().__init__()
        self.clients = []
