import os
import subprocess
import sys

# Configuration
input_folder = sys.argv[1]
hex_script = sys.argv[2]
output_file = sys.argv[3]

if not os.path.isdir(input_folder):
    raise Exception('Input folder is not a folder')

# Process files
variables = []
for filename in os.listdir(input_folder):
    if filename.lower().endswith(".png"):
        filepath = os.path.join(input_folder, filename)

        # Run the hex conversion script
        result = subprocess.run(["python3", hex_script, "--hex", "-c", "--py", "--preferred-bg=black", filepath], capture_output=True, text=True)
        hex_output = result.stdout.strip()
        hex_error = result.stderr.strip()

        if hex_error:
            print("Error:")
            print(hex_error)

        if hex_output:
            var_name = os.path.splitext(filename)[0].upper()
            variables.append(f'{var_name} = art({hex_output})')

PREFACE = """
def art(w: int, h: int, data: str, color: str):
    return bytes.fromhex("{0}{1}{2}{3}".format(w.to_bytes(1, "little").hex(), h.to_bytes(1, "little").hex(), data, color))

def art_bytes(w: int, h: int, data: bytes, color: bytes):
    return art(w, h, data.hex(), color.hex())

"""

if variables:
    with open(output_file, "w") as f:
        f.write(PREFACE)
        f.write("\n".join(variables) + "\n")

print(f"Generated {output_file} with {len(variables)} entries.")
