#!/usr/bin/python
#
# keycheck-autolab.py - Script to check validity of key.txt (for autolab use)
#
import sys;
import hashlib;


# Check for key.txt

with open("key.txt") as keyfile:
    head = [next(keyfile) for x in range(2)]

andrewID = head[0].strip()
assessMeKey =  head[1].strip()

if(andrewID == "Enter your andrew id here"):
    print "Please enter your andrew id in the first line of the key.txt. Please read the write up!"
    sys.exit(2);

if(assessMeKey == "Enter the key from AssessMe here"):
    print "Key mismatch. Please verify you have entered the key you received from AssessMe. Follow the format specified in the writeup!"
    sys.exit(2);

if(len(assessMeKey) != 32):
    print "Key mismatch. Please verify you have entered the key you received from AssessMe. Follow the format specified in the writeup!"
    sys.exit(2);

sys.exit(1);
    
    


