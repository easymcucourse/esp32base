import asyncio
from bleak import BleakScanner

async def run():
    print("Scanning for ESP32_Dice_BLE...", flush=True)
    devices = await BleakScanner.discover()
    found = False
    for d in devices:
        if d.name and "ESP32_Dice" in d.name:
            print(f"Found Device: {d.name}, Address: {d.address}", flush=True)
            found = True
    if not found:
        print("ESP32_Dice_BLE not found in scan.", flush=True)

if __name__ == "__main__":
    asyncio.run(run())
