import argparse
from pathlib import Path

from .app import SetupApp


def main():
    parser = argparse.ArgumentParser(description="Set up your Somfy HomeKit bridge over USB")
    parser.add_argument("--port", help="Select and connect to this serial port")
    parser.add_argument("--demo", action="store_true", help="Try the interface without hardware")
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2],
                        help="Firmware repository directory (for configuration and upload)")
    args = parser.parse_args()
    SetupApp(project=args.project.resolve(), port=args.port, demo=args.demo).run()


if __name__ == "__main__":
    main()
