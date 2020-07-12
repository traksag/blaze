import sys
import json
import collections

# Download the vanilla Minecraft server and run
#
#  java -cp server.jar net.minecraft.data.Main --all
#
# to generate all the data files into a folder called 'generated'. Provide the
# path to the file 'reports/registries.json' to this program to generate a list
# of in-code names of entity types.

if len(sys.argv) < 2:
    print("Please specify registries.json")
    sys.exit(0)

f = open(sys.argv[1])
registries = json.load(f, object_pairs_hook=collections.OrderedDict)
registry = registries["minecraft:entity_type"]

for key in registry["entries"].keys():
    print("ENTITY_" + key[10:].upper() + ",")
