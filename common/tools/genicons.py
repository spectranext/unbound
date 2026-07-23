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
        result = subprocess.run(["python3", hex_script, "--hex", "-c", "--nine", filepath], capture_output=True, text=True)
        hex_output = result.stdout.strip()

        if hex_output:
            var_name = os.path.splitext(filename)[0].upper()
            variables.append(f'ICON_{var_name} = bytes.fromhex("{hex_output}")')

if variables:
    with open(output_file, "w") as f:
        f.write("\n".join(variables) + "\n")

print(f"Generated {output_file} with {len(variables)} entries.")
