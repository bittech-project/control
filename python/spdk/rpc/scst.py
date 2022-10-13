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

def scst_dev_open(client, dev_name, handler, attributes):
    """Adds a new device using specified handler.
    Args:
        dev_name: SCST device name
        handler: SCST handler name
        attributes: SCST dev attributes <p=v,...>
    """
    params = {
        'subsystem': 'scst',
        'op': 'dev_open',
        'dev_name': dev_name,
        'handler': handler,
        'attributes': attributes,
    }
    return client.call('control', params)
