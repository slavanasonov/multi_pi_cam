#!/bin/sh
DIR=/home/pi/project
sleep 5
while [ 1 ]
do
	if [ -f /ramdisk/save_image_flag ]
		then
			sudo echo '0' > /ramdisk/save_image_flag
		else
			sudo mount -t tmpfs -o size=8m tmpfs /ramdisk
			sudo echo '0' > /ramdisk/save_image_flag
			sudo echo '0' > /ramdisk/average_grad.data
			sudo echo '0' > /ramdisk/average_noise.data
			sudo echo '0' > /ramdisk/rawrgb.data
			sudo chmod 777 /ramdisk/save_image_flag
			sudo chmod 777 /ramdisk/average_grad.data
			sudo chmod 777 /ramdisk/average_noise.data
			sudo chmod 777 /ramdisk/rawrgb.data
	fi
	if [ -d /export/project/pisec ]
		then
			#echo 'Remote filesystem was mounted'
			#echo 'Pisec process starting'
			$DIR/pisec.py 
			#echo 'Pisec process stopped'
		else
			#echo 'Pisec mounting remote filesystem'
			sudo mount -t cifs -o password='password' //192.168.0.200/export /export
			sleep 1
			if [ -d /export/project/pisec ]
				then
					#echo 'Remote filesystem now mounted'
					#echo 'Pisec process starting'
					$DIR/pisec.py 
					#echo 'Pisec process stopped'
				else
					#echo 'Pisec mounting fail'
					sleep 10
			fi
	fi
done
