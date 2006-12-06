#!/bin/sh

LANG=ga_IE.utf8 ./cal -3 11 2004 #truncation
LANG=zh_HK.utf8 ./cal -3         #centering
./cal | cat  #no highlight
TERM= ./cal  #no highlight
TERM=vt100 ./cal  #highlight with characters to be stripped by putp
./cal -y | head -10 | tr ' ' . #3 spaces
./cal -3 | tr ' ' .            #2 spaces ?
