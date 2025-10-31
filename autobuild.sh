#!/bin/bash

set -e

# 如果没有build目录创建该目录
if [! -d `pwd`/build]; then
    mkdir `pwd`/build
fi

rm -rf `pwd`/build/*

cd `pwd`/build &&
    cmake .. &&
    make

# 回到项目根目录
cd ..

# 把文件拷贝到 /usr/include/mymuduo so库拷贝到/usr/lib下面
if [ ! -d /usr/include/mymuduo ]; then
    sudo mkdir /usr/include/mymuduo
fi

for header in   `ls *.h`
do 
    sudo cp $header /usr/include/mymuduo
done

sudo cp `pwd`/lib/libmymuduo.so /usr/lib

sudo ldconfig