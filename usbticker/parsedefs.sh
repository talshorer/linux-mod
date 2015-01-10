#! /bin/sh
eval "$(sed 's/#define \(.\+\) \(.\+\)/\1=\2/' usbticker_defs.h)"
