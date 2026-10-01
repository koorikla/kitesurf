import json
import sys

if len(sys.argv) < 2:
    print("Usage: parse_test_report.py <index.json>")
    sys.exit(0)

try:
    with open(sys.argv[1], 'r') as f:
        data = json.load(f)
    failed = data.get('failed', 0)
    succeeded = data.get('succeeded', 0)
    total = data.get('total', 0)
    print(f"Test Results: Total={total}, Succeeded={succeeded}, Failed={failed}")
    if failed > 0:
        sys.exit(1)
except Exception as e:
    print(f"Error parsing report: {e}")
