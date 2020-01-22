import re
import os
import sys
import json
from subprocess import Popen,PIPE


def myprocess(msg,cmd_list):
    print("%-60s" % msg,end="",flush=True)
    p = Popen(cmd_list,universal_newlines=True,stdout=PIPE,stderr=PIPE)
    out,err = p.communicate()
    if len(err) != 0:
        sys.exit("\n===> Failed with error: \n%s" % (str(err)))
    print("%-10s" % "...done",flush=True)




infile = sys.argv[1]
infile = os.path.abspath(infile) # make sure this is the absolute path

dum  = infile.split("/")
path = "/".join(dum[:-1]) + "/"
name = dum[-1]

molet_home = "/home/george/myCodes/molet/"
f          = open(infile,'r')
input_str  = f.read()
input_str  = re.sub(re.compile("/\*.*?\*/",re.DOTALL),"",input_str)
input_str  = re.sub(re.compile("//.*?\n" ),"",input_str)
json_in    = json.loads(input_str)






# Step 1:
# Get angular diameter distances
####################################################################################
cmd_list = [
    "python",
    molet_home+"cosmology/angular_diameter_distances.py",
    infile,
    path
]
myprocess("Getting angular diameter distances...",cmd_list)




# Step 2:
# Get extended lensed images of the source
####################################################################################
cmd_list = [
    molet_home+"lensed_extended_source/vkl_fproject/bin/fproject",
    infile,
    path+"angular_diameter_distances.json",
    path
]
myprocess("Getting extended source lensed features...",cmd_list)




# Intermediate step:
# Get point source images
####################################################################################
if json_in["point_source"]["type"] != "none":
    cmd_list = [
        "echo",
        "'DO QUASAR STUFF'"
    ]
    myprocess("Getting point-like source lensed images...",cmd_list)  





# Step 3:
# Get light profile of the lens
####################################################################################
cmd_list = [
    molet_home+"lens_light/vkl_lens_light/bin/lens_light",
    infile,
    path+"angular_diameter_distances.json",
    path
]
myprocess("Getting light profile of the lens...",cmd_list)


    



print("Completed successfully!")
print("   Output in: ",path)
