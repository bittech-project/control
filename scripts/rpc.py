#!/usr/bin/env python3

import logging
import argparse
import importlib
import os
import sys
import shlex
import json

try:
    from shlex import quote
except ImportError:
    from pipes import quote

sys.path.append(os.path.dirname(__file__) + '/../python')

import spdk.rpc as rpc  # noqa
from spdk.rpc.client import print_dict, print_json, JSONRPCException  # noqa
from spdk.rpc.helpers import deprecated_aliases  # noqa


def print_array(a):
    print(" ".join((quote(v) for v in a)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='SPDK RPC command line interface', usage='%(prog)s [options]')
    parser.add_argument('-s', dest='server_addr',
                        help='RPC domain socket path or IP address', default='/var/tmp/spdk.sock')
    parser.add_argument('-p', dest='port',
                        help='RPC port number (if server_addr is IP address)',
                        default=5260, type=int)
    parser.add_argument('-t', dest='timeout',
                        help='Timeout as a floating point number expressed in seconds waiting for response. Default: 60.0',
                        default=60.0, type=float)
    parser.add_argument('-r', dest='conn_retries',
                        help='Retry connecting to the RPC server N times with 0.2s interval. Default: 0',
                        default=0, type=int)
    parser.add_argument('-v', dest='verbose', action='store_const', const="INFO",
                        help='Set verbose mode to INFO', default="ERROR")
    parser.add_argument('--verbose', dest='verbose', choices=['DEBUG', 'INFO', 'ERROR'],
                        help="""Set verbose level. """)
    parser.add_argument('--dry-run', dest='dry_run', action='store_true', help="Display request and exit")
    parser.set_defaults(dry_run=False)
    parser.add_argument('--server', dest='is_server', action='store_true',
                        help="Start listening on stdin, parse each line as a regular rpc.py execution and create \
                                a separate connection for each command. Each command's output ends with either \
                                **STATUS=0 if the command succeeded or **STATUS=1 if it failed. --server is meant \
                                to be used in conjunction with bash coproc, where stdin and stdout are connected to \
                                pipes and can be used as a faster way to send RPC commands. If enabled, rpc.py \
                                must be executed without any other parameters.")
    parser.set_defaults(is_server=False)
    parser.add_argument('--plugin', dest='rpc_plugin', help='Module name of plugin with additional RPC commands')
    subparsers = parser.add_subparsers(help='RPC methods', dest='called_rpc_name', metavar='')

    def rpc_get_methods(args):
        print_dict(rpc.rpc_get_methods(args.client,
                                       current=args.current,
                                       include_aliases=args.include_aliases))

    p = subparsers.add_parser('rpc_get_methods', help='Get list of supported RPC methods')
    p.add_argument('-c', '--current', help='Get list of RPC methods only callable in the current state.', action='store_true')
    p.add_argument('-i', '--include-aliases', help='include RPC aliases', action='store_true')
    p.set_defaults(func=rpc_get_methods)

    def spdk_get_version(args):
        print_json(rpc.spdk_get_version(args.client))

    p = subparsers.add_parser('spdk_get_version', help='Get SPDK version')
    p.set_defaults(func=spdk_get_version)

    # scst
    def scst_driver_init(args):
        json = rpc.scst.scst_driver_init(args.client,
                                         drivers=args.drivers)
        print_json(json)

    p = subparsers.add_parser('scst_driver_init', help='Load specified SCST drivers')
    p.add_argument('-d', '--drivers', nargs='+', help='SCST driver names', required=True, type=str)
    p.set_defaults(func=scst_driver_init)

    def scst_driver_deinit(args):
        json = rpc.scst.scst_driver_deinit(args.client,
                                           drivers=args.drivers)
        print_json(json)

    p = subparsers.add_parser('scst_driver_deinit', help='Unload specified SCST driver')
    p.add_argument('-d', '--drivers', nargs='+', help='SCST driver names', required=True, type=str)
    p.set_defaults(func=scst_driver_deinit)

    def scst_dev_open(args):
        json = rpc.scst.scst_dev_open(args.client,
                                      name=args.name,
                                      handler=args.handler,
                                      attributes=args.attributes)
        print_json(json)

    p = subparsers.add_parser('scst_dev_open', help='Adds a new device using handler <handler>')
    p.add_argument('-n', '--name', help='SCST device name', required=True, type=str)
    p.add_argument('-dh', '--handler', help='SCST handler name', required=True, type=str)
    p.add_argument('-attrs', '--attributes', nargs='+', help='SCST dev attributes <p=v,...>', required=False, type=str)
    p.set_defaults(func=scst_dev_open)

    def scst_dev_close(args):
        json = rpc.scst.scst_dev_close(args.client,
                                       name=args.name,
                                       handler=args.handler)
        print_json(json)

    p = subparsers.add_parser('scst_dev_close', help='Closes a device belonging to handler <handler>')
    p.add_argument('-n', '--name', help='SCST device name', required=True, type=str)
    p.add_argument('-dh', '--handler', help='SCST handler name', required=True, type=str)
    p.set_defaults(func=scst_dev_close)

    def scst_dev_resync(args):
        json = rpc.scst.scst_dev_resync(args.client,
                                        name=args.name)
        print_json(json)

    p = subparsers.add_parser('scst_dev_resync', help='Resync the device size with the initiator(s)')
    p.add_argument('-n', '--name', help='SCST device name', required=True, type=str)
    p.set_defaults(func=scst_dev_resync)

    def scst_handler_list(args):
        json = rpc.scst.scst_handler_list(args.client)
        print_json(json)

    p = subparsers.add_parser('scst_handler_list', help='List all available handlers')
    p.set_defaults(func=scst_handler_list)

    def scst_device_list(args):
        json = rpc.scst.scst_device_list(args.client)
        print_json(json)

    p = subparsers.add_parser('scst_device_list', help='List all open devices')
    p.set_defaults(func=scst_device_list)

    def check_called_name(name):
        if name in deprecated_aliases:
            print("{} is deprecated, use {} instead.".format(name, deprecated_aliases[name]), file=sys.stderr)

    class dry_run_client:
        def call(self, method, params=None):
            print("Request:\n" + json.dumps({"method": method, "params": params}, indent=2))

    def null_print(arg):
        pass

    def call_rpc_func(args):
        args.func(args)
        check_called_name(args.called_rpc_name)

    def execute_script(parser, client, fd):
        executed_rpc = ""
        for rpc_call in map(str.rstrip, fd):
            if not rpc_call.strip():
                continue
            executed_rpc = "\n".join([executed_rpc, rpc_call])
            rpc_args = shlex.split(rpc_call)
            if rpc_args[0][0] == '#':
                # Ignore lines starting with # - treat them as comments
                continue
            args = parser.parse_args(rpc_args)
            args.client = client
            try:
                call_rpc_func(args)
            except JSONRPCException as ex:
                print("Exception:")
                print(executed_rpc.strip() + " <<<")
                print(ex.message)
                exit(1)

    def load_plugin(args):
        # Create temporary parser, pull out the plugin parameter, load the module, and then run the real argument parser
        plugin_parser = argparse.ArgumentParser(add_help=False)
        plugin_parser.add_argument('--plugin', dest='rpc_plugin', help='Module name of plugin with additional RPC commands')

        rpc_module = plugin_parser.parse_known_args()[0].rpc_plugin
        if args is not None:
            rpc_module = plugin_parser.parse_known_args(args)[0].rpc_plugin

        if rpc_module is not None:
            try:
                rpc_plugin = importlib.import_module(rpc_module)
                try:
                    rpc_plugin.spdk_rpc_plugin_initialize(subparsers)
                except AttributeError:
                    print("Module %s does not contain 'spdk_rpc_plugin_initialize' function" % rpc_module)
            except ModuleNotFoundError:
                print("Module %s not found" % rpc_module)

    def replace_arg_underscores(args):
        # All option names are defined with dashes only - for example: --tgt-name
        # But if user used underscores, convert them to dashes (--tgt_name => --tgt-name)
        # SPDK was inconsistent previously and had some options with underscores, so
        # doing this conversion ensures backward compatibility with older scripts.
        for i in range(len(args)):
            arg = args[i]
            if arg.startswith('--') and "_" in arg:
                args[i] = arg.replace('_', '-')

    load_plugin(None)

    replace_arg_underscores(sys.argv)

    args = parser.parse_args()

    if sys.stdin.isatty() and not hasattr(args, 'func'):
        # No arguments and no data piped through stdin
        parser.print_help()
        exit(1)
    if args.is_server:
        for input in sys.stdin:
            cmd = shlex.split(input)
            replace_arg_underscores(cmd)
            try:
                load_plugin(cmd)
                tmp_args = parser.parse_args(cmd)
            except SystemExit as ex:
                print("**STATUS=1", flush=True)
                continue

            try:
                tmp_args.client = rpc.client.JSONRPCClient(
                    tmp_args.server_addr, tmp_args.port, tmp_args.timeout,
                    log_level=getattr(logging, tmp_args.verbose.upper()), conn_retries=tmp_args.conn_retries)
                call_rpc_func(tmp_args)
                print("**STATUS=0", flush=True)
            except JSONRPCException as ex:
                print(ex.message)
                print("**STATUS=1", flush=True)
        exit(0)
    elif args.dry_run:
        args.client = dry_run_client()
        print_dict = null_print
        print_json = null_print
        print_array = null_print
    else:
        try:
            args.client = rpc.client.JSONRPCClient(args.server_addr, args.port, args.timeout,
                                                   log_level=getattr(logging, args.verbose.upper()),
                                                   conn_retries=args.conn_retries)
        except JSONRPCException as ex:
            print(ex.message)
            exit(1)

    if hasattr(args, 'func'):
        try:
            call_rpc_func(args)
        except JSONRPCException as ex:
            print(ex.message)
            exit(1)
    else:
        execute_script(parser, args.client, sys.stdin)
