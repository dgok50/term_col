#!/usr/bin/env bash
killall kuip
mkdir $HOME/term_col/log
mkdir $HOME/term_col/log/kuip-valgrind/

valgrind --leak-check=no --track-origins=yes --gen-suppressions=all \
  --read-var-info=yes \
  --log-file=$HOME/term_col/log/kuip-valgrind/%p.log \
  --time-stamp=yes --trace-children=yes ./kuip -k \
  2>&1 | tee $HOME/term_col/log/kuip-valgrind/kuip.log