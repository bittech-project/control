import json
from scan.disks.disks import scan as scanDisks
from scan.pools import scan as scanPools
from scan.volumes import scan as scanVolumes
from scan.exports.nvmeof import scanNvmeof
from scan.exports.scst import scanScst
from scan.exports.nfs import scanNfs
from scan.networks.networks import scan as scanNetworks
from scan.disks.update_disks import updateDisksRelationId

disks = scanDisks()
pools = scanPools()
updateDisksRelationId(disks, pools)
volumes = scanVolumes()
nvmeof_exports = scanNvmeof()
scst_exports = scanScst()
nfs_exports = scanNfs()
networks = scanNetworks()

result = {
    "disks":  [d.__dict__ for d in disks],
    "pools": [p.__dict__ for p in pools],
    "volumes": [v.__dict__ for v in volumes],
    "nvmeof_exports": [e.__dict__ for e in nvmeof_exports],
    "scst_exports": [e.__dict__ for e in scst_exports],
    "nfs_exports": [e.__dict__ for e in nfs_exports],
    "net_interfaces": [n.__dict__ for n in networks]
}
print(json.dumps(result))
