from .cmd_parser import *


def scst_drivers_init(client, drivers):
    """Load specified SCST drivers.
    Args:
        drivers: List of a SCST driver names to load
    """
    params = {
        'subsystem': 'scst',
        'op': 'init',
        'drivers': drivers,
    }
    return client.call('control', params)

def scst_drivers_deinit(client, drivers):
    """Unload specified SCST drivers.
    Args:
        drivers: List of a SCST driver names to unload
    """
    params = {
        'subsystem': 'scst',
        'op': 'deinit',
        'drivers': drivers,
    }
    return client.call('control', params)
