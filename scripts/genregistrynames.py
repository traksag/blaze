import sys
import json
import collections

# Download the vanilla Minecraft server and run
#
#  java -cp server.jar net.minecraft.data.Main --all
#
# to generate all the data files into a folder called 'generated'. Provide the
# path to the file 'reports/registries.json' to this program to generate a list
# of in-code names of registry entries.

if len(sys.argv) < 4:
    print("Please specify registries.json, a registry and a prefix")
    sys.exit(0)

f = open(sys.argv[1])
registries = json.load(f, object_pairs_hook=collections.OrderedDict)
registry = registries["minecraft:" + sys.argv[2]]
prefix = sys.argv[3]

for key in registry["entries"].keys():
    print(prefix + key[10:].upper() + ",")
