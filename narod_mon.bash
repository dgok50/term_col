#!/bin/bash
#Original by Tuinov Andrey voytmar @ yandex.ru
#Edit by dgok50
SERVER="narodmon.ru"
PORT="8283"
kill $(cat /tmp/narod.pid)
rm -f /tmp/narod.pid


echo "$BASHPID" >> /tmp/narod.pid
echo "$BASHPID"
#exit
# идентификатор устройства, для простоты добавляется 01 (02) к MAC устройства
#SENSOR_ID_1=$DEVICE_MAC"01"
#SENSOR_ID_2=$DEVICE_MAC"02"

# значения датчиков
#sensor_value_1=20
#sensor_value_2=-20.25
MSG_IN="Error"

DATA="$(cat /usr/share/nginx/html/tmp/arduino.txt)"
if [ ! -f /usr/share/nginx/html/tmp/arduino.txt ]; then
    exit 1;
fi
while [ "$MSG_IN" != "OK" ]
do
sleep 2
# устанавливаем соединение
exec 3<>/dev/tcp/$SERVER/$PORT

# отсылаем единичное значение датчика
#printf "#b8-27-eb-8d-59-05#KUIP#55.7243#37.8174#176\n" >&3
printf "$DATA" >&3
echo "$DATA"
#printf "##" >&3
# отсылаем  множественные значения датчиков
#printf "#%s\n#%s#%s\n#%s#%s\n##" "$DEVICE_MAC" "$SENSOR_ID_1" "$sensor_value_1" "$SENSOR_ID_2" "$sensor_value_2" >&3

# получаем ответ
read -r MSG_IN <&3
echo "$MSG_IN"

# закрываем соединение
exec 3<&-
exec 3>&-
sleep 70
done
rm -f /usr/share/nginx/html/tmp/arduino.txt
rm -f /tmp/narod.pid
echo "Sucsses"
exit 0;