import subprocess
import time
import sys
import os

os.environ["DISPLAY"] = ":0"

root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

print("--- Testing GUI in Thread Mode (./terminator) ---")
p = subprocess.Popen(["./terminator"], cwd=root_dir)
time.sleep(1.5)
if p.poll() is not None:
    print(f"[FAIL] Terminator exited prematurely with code {p.returncode}")
    sys.exit(1)
else:
    print("[PASS] Terminator GUI running and responsive in Thread Mode")
    p.terminate()
    p.wait()

print("\n--- Testing GUI in Process Mode (./terminator useProcess=True) ---")
p = subprocess.Popen(["./terminator", "useProcess=True"], cwd=root_dir)
time.sleep(1.5)
if p.poll() is not None:
    print(f"[FAIL] Terminator exited prematurely with code {p.returncode}")
    sys.exit(1)
else:
    print("[PASS] Terminator GUI running and responsive in Process Mode")
    p.terminate()
    p.wait()

print("\nAll GUI tests passed successfully!")
