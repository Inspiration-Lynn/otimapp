#!/bin/bash
source `dirname $0`/util.sh
start_date=`getDate`

scen_start=1
scen_end=100
force=0
map="den520d.map"
agents_list="50 100 150 200"
time_limit=30000
exec_option="_"
exec_repetation=0

solvers=(
    "PP -m 100000"
    "CP"
    "PP -m 100000 -f 2"
    "CP -f 2"
    "PP -m 100000 -f 4"
    "CP -f 4"
    "PP -m 100000 -f 6"
    "CP -f 6"
    "PP -m 100000 -f 8"
    "CP -f 8"
)

for solver in "${solvers[@]}"
do
    bash `dirname $0`/run.sh \
       $map \
       "$agents_list" \
       "$solver" \
       $scen_start \
       $scen_end \
       $time_limit \
       $exec_option \
       $exec_repetation \
       $force
done

# send message
str_solvers="solvers="
for solver in "${solvers[@]}"
do
    str_solvers=$str_solvers"\n"$solver
done
str_solvers=${str_solvers//\"/\\\"}

MESSAGE="*-----------------------------------
fin experiment\nstress-test\ndate=${start_date}\nmap=${map}\nagents=${agents_list}\n\n${str_solvers}
-----------------------------------*"

bash `dirname $0`/slack_notification.sh "$MESSAGE"