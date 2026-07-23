import nltk
import re
import sys

COMMENTS = re.compile("^(.*?)\\s*;.*")
ASM_BYTES = re.compile(".*\\] ([a-z0-9\\s]+)")

to_sort = []


def filter_ut(line):
    if line.endswith(":"):
        return False
    if len(line) == 0:
        return False
    return True


def preprocess(line):
    line = line.strip()
    m1 = ASM_BYTES.match(line)
    m = COMMENTS.match(line)
    if m:
        line = m.group(1)

    if m1:
        amount = len(m1.group(1).replace(" ", "")) // 2
    else:
        amount = 1
    return str(amount) + "; " + line


for fname in sys.argv[1:]:
    with open(fname, "r") as f:
        data = f.readlines()
        filtered = list(map(preprocess, filter(filter_ut, data)))

        for x in range(3, 32):
            for k, v in nltk.FreqDist(nltk.ngrams(filtered, x)).items():
                if v <= 3:
                    continue
                if k.count(k[0]) == len(k):  # skip multiline identical sequences
                    continue
                weight = 0
                for line in k:
                    if ";" in line:
                        weight += int(line[0:line.index(";")])
                if weight <= 3:
                    continue
                to_sort.append((k, v, weight))

for lines, occurrence, weight in sorted(to_sort, key=lambda x: x[2] * x[1], reverse=True):
    save = weight * occurrence
    print("{0} x {1} = {2}, {3} bytes, {4} overall:".format(occurrence, weight, occurrence * weight, weight, save))
    for l in lines:
        print(l)
    print()
