import fileinput
from itertools import islice

last_address = 25000
last_item = None
last_filename = None
entries = {}
sum = {}
total_sum = 0
head = 0

for line in fileinput.input():
    address_, entry_, filename_ = line.split(",")
    entry_ = entry_.strip()
    if entry_.startswith("i_"):
        continue
    if entry_.startswith("__C_LINE"):
        continue
    if entry_.startswith("__ASM_LINE"):
        continue
    if entry_.startswith("__CDB_INFO"):
        continue
    if entry_.startswith("CFG_"):
        continue
    if entry_.startswith("__BSS_"):
        continue

    if head == 0:
        if entry_ == "start":
            head = address_
        else:
            continue

    address_ = int(address_, 16)
    if address_ > 65535:
        continue

    diff = address_ - last_address

    if diff <= 0:
        last_address = address_
        continue

    if last_filename:
        if "gui" in last_filename:
            category = "gui"
        elif "proto" in last_filename:
            category = "proto"
        elif "render" in last_filename:
            category = "render"
        elif "net.c" in last_filename:
            category = "net"
        else:
            category = "other"

        if category not in entries:
            entries[category] = []
            sum[category] = 0

        entries[category].append((last_item, diff, last_filename))
        sum[category] += diff
        total_sum += diff

    last_address = address_
    last_item = entry_
    last_filename = filename_.strip()

    if entry_ == "__BSS_END_tail":
        break

for category in entries:
    with open("sizes/{0}.txt".format(category), "w") as f:
        print("{0} (sum: {1}):".format(category, sum[category]))
        print("{0} (sum: {1}):".format(category, sum[category]), file=f)
        for key, value, filename in islice(sorted(entries[category], key=lambda x: x[1], reverse=True), 0, 32):
            print("  {0}: {1}".format(key, value))
            print("  {0}: {1}".format(key, value), file=f)

max_allowed = 65536 - 25000
print("Sum: {0} Total: {1}".format(sum, total_sum))
print("{0} bytes left".format(max_allowed - total_sum))
