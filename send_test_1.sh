#! /bin/bash

#Declare the variables
HOST=192.168.1.23
TOPIC=FD/ERROR
SLEEP=1
PROG_NAME=DUVEL
SEV_CODE=1

ERROR_CODE_ARRAY1=(  "GEN0001" "GEN0002" "GEN0003" "GEN0004" "GEN0005"
                    "AUT1001" "AUT1002" "AUT1003" "AUT1004" "AUT1005"
                    "DBE2001" "DBE2002" "DBE2003" "DBE2004" "DBE2005"
                    "NET3001" "NET3002" "NET3003" "NET3004" "NET3005"
                    "FSE4001" "FSE4002" "FSE4003" "FSE4004" "FSE4005"
                    "IOE5001" "IOE5002" "IOE5003" "IOE5004" "IOE5005"
                    "CFG6001" "CFG6002" "CFG6003" "CFG6004" "CFG6005"
                    "SYN7001" "SYN7002" "SYN7003" "SYN7004" "SYN7005"
                    "PER8001" "PER8002" "PER8003" "PER8004" "PER8005"
                    "RES9001" "RES9002" "RES9003" "RES9004" "RES9005"
                    "MEA0027" "ABC0011" "DIV0001" "MEM0001")

declare -A ERROR_CODE_ARRAY2=( [0, 0]="FSY0001" [0, 1]="TextFile.txt"
                    [1, 0]="FSY0002" [1, 1]="TextFile.txt"
                    [2, 0]="FSY0003" [2, 1]="TextFile.txt"
                    [3, 0]="FSY0004" [3, 1]="TextFile.txt"
                    [4, 0]="FSY0005" [4, 1]="TextFile.txt"
                    [5, 0]="USR0001" [5, 1]="Frank Demonie")
      

# Start the script
echo "Start of Transmission: sending to $HOST on Topic $TOPIC"
echo "=================================================================="

#Send the error messages of ARRAY 1
for ERR_CODE in "${ERROR_CODE_ARRAY1[@]}"; do
    ERR_MSG="$SEV_CODE;$PROG_NAME;$ERR_CODE"
    echo "Sending => $ERR_MSG"
    mosquitto_pub -h $HOST -t $TOPIC -m "$ERR_MSG"
    sleep $SLEEP
done    

#Send the error messages of ARRAY 2
for i in {0..5}; do
    ERR_MSG="$SEV_CODE;$PROG_NAME;${ERROR_CODE_ARRAY2[$i, 0]};${ERROR_CODE_ARRAY2[$i, 1]}"
    echo "Sending => $ERR_MSG"
    mosquitto_pub -h $HOST -t $TOPIC -m "$ERR_MSG"
    sleep $SLEEP
done

#End the script
echo "End of Transmission"
echo "=================================================================="