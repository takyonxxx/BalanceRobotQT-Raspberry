# BalanceRobot & Remote Control by Bluetooth BLE GATT Services
This is the new qt c++ version of 
https://github.com/takyonxxx/BalanceRobot-Raspberry-Pi

The remote control can compile on all platforms that supports qt.
https://www.youtube.com/watch?v=immSrXEHzQE&feature=youtu.be

Proportional Term (KP)
The proportional term is your primary term for controlling the error. this directly scales your error, so with a small KP the controller will make small attempts to minimize the error, and with a large KP the controller will make a larger attempt. If the KP is too small you might never minimize the error (unless you are using D and I terms) and not be able to respond to changes affecting your system, and if KP is too large you can have an unstable (ie. weird oscillations) filter that severely overshoot the desired value.

Integral Term (KI)
The integral term lets the controller handle errors that are accumulating over time. This is good when you need to handle errors steady state errors. The problem is that if you have a large KI you are trying to correct error over time so it can interfere with your response for dealing with current changes. This term is often the cause of instability in your PID controller.

Derivative Term (KD)
The derivative term is looking at how your system is behaving between time intervals. This helps dampen your system to improve stability. Many motor controllers will only let you configure a PI controller.

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
		
