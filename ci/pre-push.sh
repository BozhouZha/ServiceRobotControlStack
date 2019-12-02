#!/bin/bash
pushd ../../
catkin_make
res=$?
popd
if [ "$res" -ne 0 ];then
    echo -e "\033[1m\e[31mYou broke the build!!!\e[0m";
    exit 1;
fi
exit 0;
