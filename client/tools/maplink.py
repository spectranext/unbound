import sys
import re

map_file = sys.argv[1]
output_file = sys.argv[2]
prefix_only = sys.argv[3] if len(sys.argv) > 3 else None
address_symbol = re.compile("([a-zA-Z0-9_]+)\\s*=\\s*\\$([0-9A-Fa-f]+)\\s*;\\s*(.+?),")

with open(map_file, "r") as f:
    map_file_lines = f.readlines()

symbols = {}

for line in map_file_lines:
    m = address_symbol.match(line)
    if not m:
        continue
    symbol_name = m.group(1)
    symbol_address = m.group(2)
    symbol_type = m.group(3)
    if symbol_name.startswith("i_"):
        continue
    if symbol_name.startswith("__"):
        continue
    if symbol_name in ["_module_action", "_module_loop", "_module_interrupt"]:
        continue
    if prefix_only and not symbol_name.startswith(prefix_only):
        continue
    if (symbol_type != "addr") and (symbol_type != "const"):
        continue
    symbols[symbol_name] = symbol_address


with open(output_file, "w") as f:
    for symbol_name in symbols.keys():
        f.write("public {0}\n".format(symbol_name))

    f.write("\n")

    for symbol_name, symbol_value in symbols.items():
        f.write("defc {0} = ${1}\n".format(symbol_name, symbol_value))
