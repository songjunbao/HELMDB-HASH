#!/bin/bash

rm motivation_dram_bw.csv
printf "access_size, rnd_read_r, rnd_read_w, seq_read_r, seq_read_w, rnd_write_r, rnd_write_w, seq_write_r, seq_write_w, " >> motivation_dram_bw.csv
i=0
while read line; do
    if [ -n "$(echo $line | grep "=========")" ]; then          
        echo $line, $i       
        echo $one_line >> motivation_dram_bw.csv
        tmp=`echo $line | awk '{print $2}'`        
        one_line="$tmp, "
    fi

    if [ -n "$(echo $line | grep "DRAM Read.*Speed")" ]; then 
        tmp=`echo $line | awk '{print $4}'`
        one_line="$one_line $tmp,"
    fi

    if [ -n "$(echo $line | grep "DRAM Write.*Speed")" ]; then 
        tmp=`echo $line | awk '{print $4}'`
        one_line="$one_line $tmp,"
    fi
done < motivation_dram.data
echo $one_line >> motivation_dram_bw.csv

one_line=""
rm motivation_pmem_bw.csv
printf "access_size, rnd_read_r, rnd_read_w, seq_read_r, seq_read_w, rnd_write_r, rnd_write_w, seq_write_r, seq_write_w, " > motivation_pmem_bw.csv
while read line; do
    if [ -n "$(echo $line | grep "=========")" ]; then 
        echo $one_line >> motivation_pmem_bw.csv
        tmp=`echo $line | awk '{print $2}'`        
        one_line="$tmp, "
    fi

    if [ -n "$(echo $line | grep "PMEM Read.*Speed")" ]; then 
        tmp=`echo $line | awk '{print $4}'`
        one_line="$one_line $tmp,"
    fi

    if [ -n "$(echo $line | grep "PMEM Write.*Speed")" ]; then 
        tmp=`echo $line | awk '{print $4}'`
        one_line="$one_line $tmp,"
    fi
done < motivation_pmem.data
echo $one_line >> motivation_pmem_bw.csv

python3 plot.py