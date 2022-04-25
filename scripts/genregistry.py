import sys
import json
import collections

# Download the vanilla Minecraft server and run
#
#  java -DbundlerMainClass=net.minecraft.data.Main -jar server-1.18.2.jar --all
#
# to generate all the data files into a folder called 'generated'. Provide the
# path to the file 'reports/registries.json' to this program to generate a file
# with all the resource locations in a registry (in a custom format).

if len(sys.argv) < 3:
    print("Please specify registries.json and registry")
    sys.exit(0)

f = open(sys.argv[1])
registries = json.load(f, object_pairs_hook=collections.OrderedDict)
registry = registries["minecraft:" + sys.argv[2]]

for key in registry["entries"].keys():
    print("key " + key)
    print("")
