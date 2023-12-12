import pytest
import json
from Fixture import Fixture

from scan.networks.ethernet import EthResource, Info


ETHERNET_SCAN_FIXTURES = [
    Fixture("""[{"ifindex":1,"ifname":"lo","flags":["LOOPBACK","UP","LOWER_UP"],"mtu":65536,"qdisc":"noqueue","operstate":"UNKNOWN","group":"default","txqlen":1000,"link_type":"loopback","address":"00:00:00:00:00:00","broadcast":"00:00:00:00:00:00","addr_info":[{"family":"inet","local":"127.0.0.1","prefixlen":8,"scope":"host","label":"lo","valid_life_time":4294967295,"preferred_life_time":4294967295},{"family":"inet6","local":"::1","prefixlen":128,"scope":"host","valid_life_time":4294967295,"preferred_life_time":4294967295}]}]""", []),
    Fixture("""[{"ifindex":4,"ifname":"docker12345","flags":["NO-CARRIER","BROADCAST","MULTICAST","UP"],"mtu":1500,"qdisc":"noqueue","operstate":"DOWN","group":"default","link_type":"ether","address":"02:42:22:be:b2:71","broadcast":"ff:ff:ff:ff:ff:ff","addr_info":[{"family":"inet","local":"172.17.0.1","prefixlen":16,"broadcast":"172.17.255.255","scope":"global","label":"docker0","valid_life_time":4294967295,"preferred_life_time":4294967295}]}]""",
            [{'name': 'docker12345', 'mac': '02:42:22:be:b2:71', 'power': 'DOWN', 'status': 'LINKED', 'addresses': ['172.17.0.1/16'], 'id': 'docker12345024222beb271', 'maxLinkSpeedMbps': '10001', 'deviceInfo': {'fullName': 'Test device info', 'numa': '-12345', 'pci': '11:11:1234'}}])
]


@pytest.mark.parametrize("fixture", ETHERNET_SCAN_FIXTURES)
def test_scanEth(fixture: Fixture):

    deviceInfo = Info()
    deviceInfo.fullName = 'Test device info'
    deviceInfo.numa = '-12345'
    deviceInfo.pci = '11:11:1234'
    maxDeviceLinkSpeed = '10001'

    jsonObject = json.loads(fixture.input)
    result = EthResource._parse(jsonObject[0], maxDeviceLinkSpeed, deviceInfo)
    resultRaw = [result.__dict__] if result else []

    assert resultRaw == fixture.expected
