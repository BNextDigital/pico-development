#!/usr/bin/env python3
"""Print a command for the encrypted Pico BLE command characteristic (0003).

This does not connect, provision, or write the device. Use nRF Connect's hex write
for manual hardware checks; production companion clients should follow clock.md.
"""
import argparse
from datetime import datetime
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    sync = commands.add_parser('sync', help='Current computer time and timezone offset')
    sync.add_argument('--offset-minutes', type=int)
    alarm = commands.add_parser('alarm')
    alarm.add_argument('--slot', type=int, choices=range(4), required=True)
    alarm.add_argument('--time', required=True, help='Local 24-hour HH:MM')
    alarm.add_argument('--repeat', choices=['once', 'daily', 'weekdays', 'weekend'], default='once')
    alarm.add_argument('--disabled', action='store_true')
    options = commands.add_parser('options')
    options.add_argument('--12-hour', dest='hour12', action='store_true')
    options.add_argument('--glance', action='store_true')
    options.add_argument('--usb-desk', action='store_true')
    args = parser.parse_args()
    if args.command == 'sync':
        offset = args.offset_minutes
        if offset is None:
            offset = int(datetime.now().astimezone().utcoffset().total_seconds() // 60)
        utc = int(time.time())
        if not -840 <= offset <= 840 or not 946684800 <= utc <= 4102444799:
            parser.error('Supported range: 2000–2099, UTC offset -840..840 minutes')
        packet = b'\x60' + struct.pack('<Ih', utc, offset)
    elif args.command == 'alarm':
        try:
            hour, minute = (int(x) for x in args.time.split(':'))
            if not (0 <= hour <= 23 and 0 <= minute <= 59):
                raise ValueError()
        except ValueError:
            parser.error('--time must be a valid HH:MM')
        weekdays = {'once': 0, 'daily': 127, 'weekdays': 31, 'weekend': 96}[args.repeat]
        packet = bytes([0x62, args.slot]) + struct.pack('<HBB', hour * 60 + minute, weekdays, not args.disabled)
    else:
        packet = bytes([0x61, (0 if args.hour12 else 1) | (2 if args.glance else 0) | (4 if args.usb_desk else 0)])
    print(packet.hex(' '))


if __name__ == '__main__':
    main()
