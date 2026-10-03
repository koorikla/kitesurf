#!/usr/bin/env python3
"""
Scan Unreal Engine game log for critical defects:
- Handled ensure
- VK_ERROR
- Fatal error
- LoadErrors:
- Material ... missing usage flag (the mesh renders with the default material)
"""
import sys
import os
import re

PATTERNS = [
    re.compile(r"Handled ensure"),
    re.compile(r"VK_ERROR"),
    re.compile(r"Fatal error"),
    re.compile(r"LoadErrors:"),
    re.compile(r"missing usage flag"),
]

def main():
    log_path = sys.argv[1] if len(sys.argv) > 1 else "Saved/Logs/KiteSurf.log"
    if not os.path.exists(log_path):
        print(f"ERROR: Log file not found: {log_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Scanning log: {log_path}")
    matched_lines = []
    
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line_num, line in enumerate(f, 1):
            line_str = line.strip()
            for pat in PATTERNS:
                if pat.search(line_str):
                    matched_lines.append((line_num, pat.pattern, line_str))
                    break

    if matched_lines:
        print(f"\nFAILED: Found {len(matched_lines)} error(s) matching patterns in {log_path}:\n", file=sys.stderr)
        for line_num, pat, line_str in matched_lines:
            print(f"[{pat}] Line {line_num}: {line_str}", file=sys.stderr)
        sys.exit(1)
    else:
        print(f"SUCCESS: No critical errors found in {log_path}.")
        sys.exit(0)

if __name__ == "__main__":
    main()
