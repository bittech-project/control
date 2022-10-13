from .cmd_parser import *


def scst_driver_init(client, drivers):
    """Load specified SCST drivers.
    Args:
        drivers: List of a SCST driver names to load
    """
    params = {
        'subsystem': 'scst',
        'op': 'driver_init',
        'drivers': drivers,
    }
    return client.call('control', params)

def scst_driver_deinit(client, drivers):
    """Unload specified SCST drivers.
    Args:
        drivers: List of a SCST driver names to unload
    """
    params = {
        'subsystem': 'scst',
        'op': 'driver_deinit',
        'drivers': drivers,
    }
    return client.call('control', params)
