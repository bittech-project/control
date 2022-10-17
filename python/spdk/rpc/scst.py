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

def scst_dev_open(client, name, handler, attributes):
    """Adds a new device using specified handler.
    Args:
        name: SCST device name
        handler: SCST handler name
        attributes: SCST dev attributes <p=v,...>
    """
    params = {
        'subsystem': 'scst',
        'op': 'dev_open',
        'name': name,
        'handler': handler,
        'attributes': attributes,
    }
    return client.call('control', params)

def scst_dev_close(client, name, handler):
    """Closes a device belonging to handler <handler>.
    Args:
        name: SCST device name
        handler: SCST handler name
    """
    params = {
        'subsystem': 'scst',
        'op': 'dev_close',
        'name': name,
        'handler': handler,
    }
    return client.call('control', params)

def scst_dev_resync(client, name):
    """Resync the device size with the initiator(s).
    Args:
        name: SCST device name
    """
    params = {
        'subsystem': 'scst',
        'op': 'dev_resync',
        'name': name,
    }
    return client.call('control', params)
