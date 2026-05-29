#!/usr/bin/env python3
"""Simulate breathalyzer serial output for testing the AI bridge."""

import argparse
import os
import pty
import time


def simulate(output_path: str):
    """Write simulated breathalyzer serial data to a pseudo-terminal."""
    events = [
        # (delay_seconds, line)
        (0.5, "LOG,500,WARMING,850,0%"),
        (1.0, "LOG,30000,WARMING,820,0%"),
        (3.0, "LOG,30000,SAFE,920,0%"),
        (2.0, "STATE=SAFE,RAW=920,AVG=920,BASE=800,W=1050,D=2300,LVL=0"),
        (2.0, "LOG,35000,WARNING,1850,68%"),
        (0.5, "STATE=WARNING,RAW=1850,AVG=1850,BASE=800,W=1050,D=2300,LVL=68"),
        (3.0, "LOG,38000,DANGER,2500,145%"),
        (0.5, "STATE=DANGER,RAW=2500,AVG=2500,BASE=800,W=1050,D=2300,LVL=145"),
        (3.0, "LOG,41000,COOLDOWN,1600,50%"),
        (0.5, "STATE=COOLDOWN,RAW=1600,AVG=1600,BASE=800,W=1050,D=2300,LVL=50"),
        (3.0, "LOG,44000,SAFE,950,2%"),
        (0.5, "STATE=SAFE,RAW=950,AVG=950,BASE=800,W=1050,D=2300,LVL=2"),
    ]

    # Print the pty path for the bridge to connect to
    print(f"SIMULATOR_PORT={output_path}")
    print("模拟器已启动，按 Ctrl+C 停止\n")

    try:
        with open(output_path, "w") as f:
            for delay, line in events:
                time.sleep(delay)
                f.write(line + "\r\n")
                f.flush()
                print(f"  -> {line}")
            # Keep running
            while True:
                time.sleep(1)
    except KeyboardInterrupt:
        print("\n模拟器停止。")


def main():
    parser = argparse.ArgumentParser(description="Breathalyzer serial simulator")
    parser.add_argument("--port", default=None, help="PTY path to write to (creates one if omitted)")
    args = parser.parse_args()

    if args.port:
        simulate(args.port)
    else:
        master_fd, slave_name = pty.openpty()
        print(f"PTY created: {slave_name}")
        try:
            simulate(slave_name)
        finally:
            os.close(master_fd)


if __name__ == "__main__":
    main()
