import sys
import json
import collections

# Download the vanilla Minecraft server and run
#
#  java -cp server.jar net.minecraft.data.Main --all
#
# to generate all the data files into a folder called 'generated'. Provide the
# path to the file 'reports/blocks.json' to this program to convert the block
# state data to our own (less verbose) format.

if len(sys.argv) < 2:
    print("Please specify blocks.json")
    sys.exit(0)

f = open(sys.argv[1])
blocks = json.load(f, object_pairs_hook=collections.OrderedDict)

for key in blocks.keys():
    print("key " + key)
    block = blocks[key]

    if "properties" in block:
        for property in block["properties"].keys():
            values = block["properties"][property]
            print("property " + property + " " + " ".join(values))
        for state in block["states"]:
            if "default" in state and state["default"]:
                properties = state["properties"] if "properties" in state else {}
                print("default_values " + " ".join(properties.values()))

    print("")
