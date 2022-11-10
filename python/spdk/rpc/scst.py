from .cmd_parser import *

def scst_handler_list(client):
    """List all available handlers.
    Args:
    """
    params = {
        'subsystem': 'scst',
        'op': 'handler_list',
    }
    return client.call('control', params)

def scst_driver_list(client):
    """List all available drivers.
    Args:
    """
    params = {
        'subsystem': 'scst',
        'op': 'driver_list',
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
    }

    if attributes is not None:
        params['attributes'] =  attributes

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

def scst_dev_list(client):
    """List all open devices.
    Args:
    """
    params = {
        'subsystem': 'scst',
        'op': 'dev_list',
    }
    return client.call('control', params)

def scst_dgrp_add(client, name):
    """Add device group <dgrp>.
    Args:
        name: SCST device group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'dgrp_add',
        'name': name,
    }
    return client.call('control', params)

def scst_dgrp_del(client, name):
    """Remove device group <dgrp>.
    Args:
        name: SCST device group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'dgrp_del',
        'name': name,
    }
    return client.call('control', params)

def scst_dgrp_list(client):
    """List all device groups.
    Args:
    """
    params = {
        'subsystem': 'scst',
        'op': 'dgrp_list',
    }
    return client.call('control', params)

def scst_dgrp_add_dev(client, name, dev_name):
    """Add device <dev_name> to device group <name>.
    Args:
        name: SCST device group name
        dev_name: SCST device name
    """
    params = {
        'subsystem': 'scst',
        'op': 'dgrp_add_dev',
        'name': name,
        'dev_name': dev_name,
    }
    return client.call('control', params)

def scst_dgrp_del_dev(client, name, dev_name):
    """Remove device <dev_name> from device group <name>.
    Args:
        name: SCST device group name
        dev_name: SCST device name
    """
    params = {
        'subsystem': 'scst',
        'op': 'dgrp_del_dev',
        'name': name,
        'dev_name': dev_name,
    }
    return client.call('control', params)

def scst_tgrp_add(client, name, dgrp_name):
    """Add target group <name> to device group <dgrp_name>.
    Args:
        name: SCST target group name
        dgrp_name: SCST device group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'tgrp_add',
        'name': name,
        'dgrp_name': dgrp_name,
    }
    return client.call('control', params)

def scst_tgrp_del(client, name, dgrp_name):
    """Remove target group <name> from device group <dgrp_name>.
    Args:
        name: SCST target group name
        dgrp_name: SCST device group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'tgrp_del',
        'name': name,
        'dgrp_name': dgrp_name,
    }
    return client.call('control', params)

def scst_tgrp_list(client, dgrp):
    """List all target groups within a device group.
    Args:
        dgrp: SCST device group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'tgrp_list',
        'dgrp': dgrp
    }
    return client.call('control', params)

def scst_tgrp_add_tgt(client, tgt_name, dgrp_name, tgrp_name):
    """Add target <tgt_name> to specified target group.
    Args:
        tgt_name: SCST target name
        dgrp_name: SCST device group name
        tgrp_name: SCST target group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'tgrp_add_tgt',
        'tgt_name': tgt_name,
        'dgrp_name': dgrp_name,
        'tgrp_name': tgrp_name,
    }
    return client.call('control', params)

def scst_tgrp_del_tgt(client, tgt_name, dgrp_name, tgrp_name):
    """Remove target <tgt_name> from specified target group.
    Args:
        tgt_name: SCST target name
        dgrp_name: SCST device group name
        tgrp_name: SCST target group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'tgrp_del_tgt',
        'tgt_name': tgt_name,
        'dgrp_name': dgrp_name,
        'tgrp_name': tgrp_name,
    }
    return client.call('control', params)

def scst_target_add(client, target, driver):
    """Add a dynamic target to a capable driver.
    Args:
        target: SCST target name
        driver: SCST target driver name
    """
    params = {
        'subsystem': 'scst',
        'op': 'target_add',
        'target': target,
        'driver': driver,
    }
    return client.call('control', params)

def scst_target_del(client, target, driver):
    """Remove a dynamic target from a driver.
    Args:
        target: SCST target name
        driver: SCST target driver name
    """
    params = {
        'subsystem': 'scst',
        'op': 'target_del',
        'target': target,
        'driver': driver,
    }
    return client.call('control', params)

def scst_target_list(client, driver):
    """List all available targets.
    Args:
    """
    params = {
        'subsystem': 'scst',
        'op': 'target_list',
    }

    if driver is not None:
        params['driver'] = driver

    return client.call('control', params)

def scst_target_enable(client, target, driver):
    """Enable target mode for a given driver & target.
    Args:
        target: SCST target name
        driver: SCST target driver name
    """
    params = {
        'subsystem': 'scst',
        'op': 'target_enable',
        'target': target,
        'driver': driver,
    }
    return client.call('control', params)

def scst_target_disable(client, target, driver):
    """Disable target mode for a given driver & target.
    Args:
        target: SCST target name
        driver: SCST target driver name
    """
    params = {
        'subsystem': 'scst',
        'op': 'target_disable',
        'target': target,
        'driver': driver,
    }
    return client.call('control', params)

def scst_group_add(client, group, target, driver):
    """Add a group to a given driver & target.
    Args:
        group: SCST group name
        target: SCST target name
        driver: SCST target driver name
    """
    params = {
        'subsystem': 'scst',
        'op': 'group_add',
        'group': group,
        'target': target,
        'driver': driver,
    }
    return client.call('control', params)

def scst_group_del(client, group, target, driver):
    """Remove a group from a given driver & target.
    Args:
        group: SCST group name
        target: SCST target name
        driver: SCST target driver name
    """
    params = {
        'subsystem': 'scst',
        'op': 'group_del',
        'group': group,
        'target': target,
        'driver': driver,
    }
    return client.call('control', params)

def scst_lun_add(client, lun, driver, target, group, device, attributes):
    """Adds a given device to a group.
    Args:
        lun: LUN number
        driver: SCST driver name
        target: SCST target name
        group: SCST group name
        device: SCST device name
        attributes: SCST dev attributes <p=v,...>
    """
    params = {
        'subsystem': 'scst',
        'op': 'lun_add',
        'lun': lun,
        'driver': driver,
        'target': target,
        'device': device,
    }

    if group is not None:
        params['group'] = group
    if attributes is not None:
        params['attributes'] =  attributes

    return client.call('control', params)

def scst_lun_del(client, lun, driver, target, group):
    """Remove a LUN from a group.
    Args:
        lun: LUN number
        driver: SCST driver name
        target: SCST target name
        group: SCST group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'lun_del',
        'lun': lun,
        'driver': driver,
        'target': target,
    }

    if group is not None:
        params['group'] = group

    return client.call('control', params)

def scst_lun_replace(client, lun, driver, target, group, device, attributes):
    """Replaces a LUN's device with a different one.
    Args:
        lun: LUN number
        driver: SCST driver name
        target: SCST target name
        group: SCST group name
        device: SCST device name
        attributes: SCST dev attributes <p=v,...>
    """
    params = {
        'subsystem': 'scst',
        'op': 'lun_replace',
        'lun': lun,
        'driver': driver,
        'target': target,
        'device': device,
    }

    if group is not None:
        params['group'] = group
    if attributes is not None:
        params['attributes'] =  attributes

    return client.call('control', params)

def scst_lun_clear(client, driver, target, group):
    """Clear all LUNs within a group.
    Args:
        driver: SCST driver name
        target: SCST target name
        group: SCST group name
    """
    params = {
        'subsystem': 'scst',
        'op': 'lun_clear',
        'driver': driver,
        'target': target,
    }

    if group is not None:
        params['group'] = group

    return client.call('control', params)
