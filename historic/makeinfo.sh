#!/bin/sh
emacs -batch $* -f texinfo-format-buffer -f save-buffer
