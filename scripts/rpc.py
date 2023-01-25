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
        description='Control RPC command line interface', usage='%(prog)s [options]')
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

    def control_set_params(params, data):
        if data is None:
            return

        try:
            params_map = {k: v for k, v in (p.split('=', 1) for p in data)}
        except ValueError:
            print("'params' format not correct")
            print("expected format: space-separated key=value pairs")
            exit(1)

        for k, v in params_map.items():
            if k in params.keys():
                print("{} parameter already set".format(k))
                exit(1)

            params[k] = v

    def control_module(args):
        params = {
            'module': args.module,
            'op': args.operation,
        }

        control_set_params(params, args.params)

        json = args.client.call('control', params)
        print_json(json)

    p = subparsers.add_parser('control', help='Send RPC request to the Control')

    p.add_argument('-m', '--module',
                   help='Control module name',
                   required=True, type=str)
    p.add_argument('-op', '--operation',
                   help='Control module operation name',
                   required=True, type=str)
    p.add_argument('-p', '--params', metavar="KEY=VALUE", nargs='+',
                   help="Set a number of key-value pairs "
                   "(do not put spaces before or after the = sign). "
                   "If a value contains spaces, you should define "
                   "it with double quotes: 'foo=\"this is a sentence\"'",
                   required=False)
    p.set_defaults(func=control_module)

    def spdk_get_version(args):
        print_json(rpc.spdk_get_version(args.client))

    p = subparsers.add_parser('spdk_get_version', help='Get SPDK version')
    p.set_defaults(func=spdk_get_version)

    # log
    def log_set_flag(args):
        rpc.log.log_set_flag(args.client, flag=args.flag)

    p = subparsers.add_parser('log_set_flag', help='set log flag')
    p.add_argument(
        'flag', help='log flag we want to set. (for example "nvme").')
    p.set_defaults(func=log_set_flag)

    def log_clear_flag(args):
        rpc.log.log_clear_flag(args.client, flag=args.flag)

    p = subparsers.add_parser('log_clear_flag', help='clear log flag')
    p.add_argument(
        'flag', help='log flag we want to clear. (for example "nvme").')
    p.set_defaults(func=log_clear_flag)

    def log_get_flags(args):
        print_dict(rpc.log.log_get_flags(args.client))

    p = subparsers.add_parser('log_get_flags', help='get log flags')
    p.set_defaults(func=log_get_flags)

    def log_set_level(args):
        rpc.log.log_set_level(args.client, level=args.level)

    p = subparsers.add_parser('log_set_level', help='set log level')
    p.add_argument('level', help='log level we want to set. (for example "DEBUG").')
    p.set_defaults(func=log_set_level)

    def log_get_level(args):
        print_dict(rpc.log.log_get_level(args.client))

    p = subparsers.add_parser('log_get_level', help='get log level')
    p.set_defaults(func=log_get_level)

    def log_set_print_level(args):
        rpc.log.log_set_print_level(args.client, level=args.level)

    p = subparsers.add_parser('log_set_print_level', help='set log print level')
    p.add_argument('level', help='log print level we want to set. (for example "DEBUG").')
    p.set_defaults(func=log_set_print_level)

    def log_get_print_level(args):
        print_dict(rpc.log.log_get_print_level(args.client))

    p = subparsers.add_parser('log_get_print_level', help='get log print level')
    p.set_defaults(func=log_get_print_level)

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
