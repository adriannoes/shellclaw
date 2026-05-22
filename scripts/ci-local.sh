#!/usr/bin/env bash
# Mirrors .github/workflows/ci.yml (excluding optional asap-compliance pip install).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
export CI=true
export GATEWAY=1
echo "==> cppcheck (make static)"
make static
echo "==> clean + test (CI=true, -Werror)"
make clean && make test
echo "==> AddressSanitizer + UBSan"
make clean
CFLAGS="-std=c11 -Wall -Wextra -Werror -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer" \
LDFLAGS="-fsanitize=address,undefined" CC="${CC:-cc}" \
make test
echo "==> release build"
make clean && make release
size=$(stat -f%z build/shellclaw 2>/dev/null || stat -c%s build/shellclaw)
max=$((2 * 1024 * 1024))
if [ "$size" -gt "$max" ]; then
	echo "Binary size $size bytes exceeds ${max} bytes"
	exit 1
fi
echo "Binary size: $size bytes (OK)"
echo "==> coverage"
make coverage
echo "==> ci-local: all steps passed"
