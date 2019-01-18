# BalanceRobot & Remote Control by Bluetooth BLE GATT Services
The new qt c++ version of 
https://github.com/takyonxxx/BalanceRobot-Raspberry-Pi

The remote control can compile on all platforms that supports qt.

https://www.youtube.com/watch?v=immSrXEHzQE&feature=youtu.be

AutoStart the app on boot:

I assume that your code and exec (bin) will be in /home/pi/BalanceRobotPi folder.

create a script start.sh on your home folder (home/pi)
place below code in it.

#!/bin/bash</br>
sudo chown root.root /home/pi/BalanceRobotPi/BalanceRobotPi</br>
sudo chmod 4755 /home/pi/BalanceRobotPi/BalanceRobotPi</br>
cd /home/pi/BalanceRobotPi</br>
./BalanceRobotPi</br>

Open a sample unit file using the command as shown below:</br>
sudo nano /lib/systemd/system/startrobot.service</br>
Add in the following text :</br>

[Unit]</br>
Description=My Robot Service</br>
After=multi-user.target</br>
</br>
[Service]</br>
Type=idle</br>
ExecStart=/home/pi/start.sh</br>
</br>
[Install]</br>
WantedBy=multi-user.target</br>
</br>
You should save and exit the nano editor.</br>

The permission on the unit file needs to be set to 644 :</br>
sudo chmod 644 /lib/systemd/system/startrobot.service</br>

Configure systemd</br>
</br>
Now the unit file has been defined we can tell systemd to start it during the boot sequence :</br>
sudo systemctl daemon-reload</br>
sudo systemctl enable startrobot.service</br>
Reboot the Pi and your custom service should run:</br>
sudo reboot</br>

<p align="center"><a href="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote.jpg">
		<img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote.jpg" 
		name="remote" width="480" height="800" align="bottom" border="1"></a></p>
		
