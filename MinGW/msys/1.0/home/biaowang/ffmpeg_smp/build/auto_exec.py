#!/bin/env python
#author BiaoWang
#execute decoding for a directory, recursively
  

import sys
import os
import subprocess
import argparse
from subprocess import Popen, PIPE, STDOUT

class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'

    def disable(self):
        self.HEADER = ''
        self.OKBLUE = ''
        self.OKGREEN = ''
        self.WARNING = ''
        self.FAIL = ''
        self.ENDC = ''

if len(sys.argv)<7:
    print "usage:python auto_exec --srcdir vidoedir --dstdir resultdir --gpumodes [1-8]"
    print len(sys.argv)
    sys.exit(-1)

parser = argparse.ArgumentParser(description='automatically decoding h264 videos in one directory.')
parser.add_argument('--srcdir',action="store", dest="rootdir"  ,help='the source directory for the decoding vidoes')
parser.add_argument('--dstdir',action="store", dest="resultdir",help='the output directory for the experiments log files')
parser.add_argument('--gpumodes',nargs='+',dest="gpumodes",help='leave the user to specify the gpumodes')

args  = parser.parse_args()
rootdir   = args.rootdir
resultdir = args.resultdir
gpumodes = args.gpumodes;
for mode in gpumodes:
    if int(mode) not in range(1,9):
        print "gpumodes currently only support 1-8"
        sys.exit(-1)
if not os.path.isdir(resultdir):
    if not '-' in resultdir:
        print "please show configuration and platform of your code in format CPU-[SIMD|SCALAR]-GPU-[CUDA|OPENCL]"
        sys.exit(-1)
    else:
        os.system("mkdir "+resultdir)
        print  bcolors.OKGREEN + "Making directory "+resultdir+" for you"+bcolors.ENDC
#collecting executing input files
fileList = []
for root, subFolders, files in os.walk(rootdir):
    for fileitems in files:
		if fileitems.endswith("h264") and "intra_only" not in fileitems:
			fileList.append(os.path.join(root,fileitems))
execfiles = sorted(fileList);
#collecting revision info 
p =  Popen("hg id -n", shell=True, stdin=PIPE, stdout=PIPE, stderr=STDOUT, close_fds=True)
Revision = p.stdout.read().strip()
if "+" in Revision:
	print bcolors.WARNING+"Warning: revision not clean, please check the revision"+Revision+bcolors.ENDC
else:
	print "Execute Revision "+Revision+" for all files"

runs  = (range(5))
static_cmd = " -t 1 -p 3 --gpu-mode "

print "output log file format:Revision-mode-resolution-coding-filename-run"
for inputfile in execfiles:
	for mode in gpumodes:
		path,filename          = os.path.split(inputfile)
		exfilepath,coding = os.path.split(path)
		excodingpath,resolution= os.path.split(exfilepath)
		explogfile = os.path.join(resultdir,resultdir+"-"+Revision+"-"+mode+"-"+resolution+"-"+coding+"-"+filename+".txt")
		print explogfile
		f = open(explogfile,"w")		
		resultlist=[]
		for r in runs:
			cmd = "./h264dec -i "+inputfile+static_cmd+mode+" 2>&1 |grep Profile"
			print cmd
			p =  Popen(cmd, shell=True, stdin=PIPE, stdout=PIPE, stderr=STDOUT, close_fds=True)
			#NOTE we assume the filename is alwayse consist of RES+CODINGMODE+NAME
			resultlist.append(p.stdout.read())
		f.writelines(resultlist)
