# import pytest
# from scan_nvmeof import startParsing, RowAccessPoint

# @pytest.mark.parametrize("nvmetcliRaw, expected", [
#     (
#     [RowAccessPoint._from({
#     "name": "dev-mapper-ttt-lvol1",
#     "devicePath": "/dev/mapper/ttt-lvol1",
#     "ipAddress": "192.168.84.148",
#     "transportProto": "tcp",
#     "port": 4421,
#     }),RowAccessPoint._from({
#     "name": "dev-mapper-ttt-lvol2",
#     "devicePath": "/dev/mapper/ttt-lvol2",
#     "ipAddress": "192.168.84.148",
#     "transportProto": "tcp",
#     "port": 4422,
#     }),RowAccessPoint._from({
#     "name": "dev-mapper-ttt-lvol0",
#     "devicePath": "/dev/mapper/ttt-lvol0",
#     "ipAddress": "192.168.84.148",
#     "transportProto": "tcp",
#     "port": 4420,
#     })],[{
#         "id": "tcp192168841484421dev-mapper-ttt-lvol1-NVME",
#         "devicePath": "/dev/mapper/ttt-lvol1",
#         "type": "NVME" ,
#         "exportPath": "tcp://192.168.84.148:4421/dev-mapper-ttt-lvol1",
#     },{
#         "id": "tcp192168841484422dev-mapper-ttt-lvol2-NVME",
#         "devicePath": "/dev/mapper/ttt-lvol2",
#         "type": "NVME" ,
#         "exportPath": "tcp://192.168.84.148:4422/dev-mapper-ttt-lvol2",
#     },{
#         "id": "tcp192168841484420dev-mapper-ttt-lvol0-NVME",
#         "devicePath": "/dev/mapper/ttt-lvol0",
#         "type": "NVME",
#         "exportPath": "tcp://192.168.84.148:4420/dev-mapper-ttt-lvol0",
#     }])
# ])
# def test_scan_nvmeOf(nvmetcliRaw, expected):
#     assert startParsing(nvmetcliRaw) == expected
