# BalanceRobot & Remote Control by Bluetooth BLE GATT Services
The new qt c++ version of 
https://github.com/takyonxxx/BalanceRobot-Raspberry-Pi

The remote control can compile on all platforms that supports qt.

https://www.youtube.com/watch?v=immSrXEHzQE&feature=youtu.be

<p align="center"><a href="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote.JPG">
		<img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote.JPG" 
		name="remote" width="480" height="800" align="bottom" border="1"></a></p>
		
		AutoStart the app on boot:

I assume that your code and exec (bin) will be in /home/pi/BalanceRobotPi folder.

create a script start.sh on your home folder (home/pi)
place below code in it.

#!/bin/bash
sudo chown root.root /home/pi/BalanceRobotPi/BalanceRobotPi
sudo chmod 4755 /home/pi/BalanceRobotPi/BalanceRobotPi
cd /home/pi/BalanceRobotPi
./BalanceRobotPi

Open a sample unit file using the command as shown below:

sudo nano /lib/systemd/system/startrobot.service
Add in the following text :

[Unit]
Description=My Robot Service
After=multi-user.target

[Service]
Type=idle
ExecStart=/home/pi/start.sh

[Install]
WantedBy=multi-user.target

You should save and exit the nano editor.

The permission on the unit file needs to be set to 644 :
sudo chmod 644 /lib/systemd/system/startrobot.service

Configure systemd
Now the unit file has been defined we can tell systemd to start it during the boot sequence :
sudo systemctl daemon-reload
sudo systemctl enable startrobot.service
Reboot the Pi and your custom service should run:
sudo reboot
