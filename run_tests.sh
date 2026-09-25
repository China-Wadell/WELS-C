#!/bin/bash
# WELS-C 测试套件
cd "$(dirname "$0")"

PASS=0
FAIL=0
FAILED=""

for f in tests/*.wec; do
    name=$(basename "$f" .wec)
    if ! ./wescc "$f" -o /tmp/welsc_test.s > /dev/null 2>&1; then
        echo "FAIL(compile) $name"
        FAIL=$((FAIL+1))
        FAILED="$FAILED $name"
        continue
    fi
    if ! gcc /tmp/welsc_test.s -o /tmp/welsc_test 2>/dev/null; then
        echo "FAIL(link)    $name"
        FAIL=$((FAIL+1))
        FAILED="$FAILED $name"
        continue
    fi
    if /tmp/welsc_test > /dev/null 2>&1; then
        PASS=$((PASS+1))
    else
        echo "FAIL(run)     $name"
        FAIL=$((FAIL+1))
        FAILED="$FAILED $name"
    fi
done

echo ""
echo "===================="
echo "通过: $PASS  失败: $FAIL"
if [ -n "$FAILED" ]; then
    echo "失败清单:$FAILED"
fi
