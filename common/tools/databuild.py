import yaml
import sys
import os
import subprocess


this_dir = os.path.dirname(os.path.abspath(__file__))
png2c_dir = os.path.join(this_dir, "png2c", "png2c.py")

with open(sys.argv[1]) as f:
    # use safe_load instead load
    data = yaml.safe_load(f)


output = ""


def wrap_value(v):
    try:
        int(v)
    except ValueError:
        return "\"{0}\"".format(v)
    else:
        return "{0}".format(v)


def generate_png2c(args):
    global output
    name = args["name"]
    result_args = f"python3 {png2c_dir} {name}"
    if "id" in args:
        result_args += " --id {0}".format(args["id"])
    if "hex" in args and args["hex"]:
        result_args += " --hex"
    if "dump-color" in args and args["dump-color"]:
        result_args += " --dump-color"
    if "pre-shift" in args and args["pre-shift"]:
        result_args += " --pre-shift"
    if "full-row" in args and args["full-row"]:
        result_args += " --full-row"
    if "extra" in args:
        extra = args["extra"]
        extra_v = [
            f"{k}={wrap_value(v)}"
            for k, v in extra.items()
        ]
        result_args += " --extra=\"{0}\"".format(";".join(extra_v))
    if "preferred-bg" in args:
        result_args += " --preferred-bg={0}".format(args["preferred-bg"])
    if "preferred-fg" in args:
        result_args += " --preferred-fg={0}".format(args["preferred-fg"])
    p = subprocess.Popen(result_args, stdout=subprocess.PIPE, shell=True)
    out, err = p.communicate()
    output += out.decode()


def new_entry(name, value, extra):
    global output
    output += "{0}={1}\n".format(name.upper(), value)
    if extra:
        for k, v in extra.items():
            output += "    {0}={1}\n".format(k, wrap_value(v))


def generate_module(args):
    client_bin = os.environ.get("CLIENT_BIN", ".")
    name = os.path.join(client_bin, args["name"])
    with open(name, "rb") as f1:
        bb = f1.read()
    if "extra" in args:
        extra = args["extra"]
    else:
        extra = {}
    if "fill" in args:
        fill = int(args["fill"])
        sz = len(bb)
        if sz < fill:
            fill -= sz
            bb += b"\0" * fill
        elif sz > fill:
            print("Error: size is bigger than fill: {0}".format(sz), file=sys.stderr)
            exit(1)
    new_entry(args["id"].upper(), bb.hex().upper(), extra)


for entry in data["data"]:
    t = entry["type"]
    if t == "png2c":
        generate_png2c(entry)
    elif t == "module":
        generate_module(entry)
    else:
        print("Error: unknown type {0}".format(t), file=sys.stderr)
        exit(1)

with open(sys.argv[2], "a" if "APPEND" in os.environ else "w") as f:
    f.write(output)
