# BalanceRobot & Remote Control by Bluetooth BLE GATT Services
This is the new qt c++ version of 
https://github.com/takyonxxx/BalanceRobot-Raspberry-Pi

The remote control can compile on all platforms that supports qt.</br>
https://www.youtube.com/watch?v=immSrXEHzQE&feature=youtu.be</br>

<b>Proportional Term (KP)</b></br>
The proportional term is your primary term for controlling the error. this directly scales your error, so with a small KP the controller will make small attempts to minimize the error, and with a large KP the controller will make a larger attempt. If the KP is too small you might never minimize the error (unless you are using D and I terms) and not be able to respond to changes affecting your system, and if KP is too large you can have an unstable (ie. weird oscillations) filter that severely overshoot the desired value.
</br></br>
<b>Integral Term (KI)</b></br>
The integral term lets the controller handle errors that are accumulating over time. This is good when you need to handle errors steady state errors. The problem is that if you have a large KI you are trying to correct error over time so it can interfere with your response for dealing with current changes. This term is often the cause of instability in your PID controller.
</br></br>
<b>Derivative Term (KD)</b></br>
The derivative term is looking at how your system is behaving between time intervals. This helps dampen your system to improve stability. Many motor controllers will only let you configure a PI controller.
</br></br>
<b>How AutoStart the app on boot?</b></br>
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

<b>Configure Systemd</b></br>
</br>
Now the unit file has been defined we can tell systemd to start it during the boot sequence :</br>
sudo systemctl daemon-reload</br>
sudo systemctl enable startrobot.service</br>
Reboot the Pi and your custom service should run:</br>
sudo reboot</br>

<b>Building code: </b></br>
Please install build-essential, alsa , qmake and espeak before compile.</br>

sudo apt-get update && sudo apt-get upgrade </br>
sudo apt-get install qt5-default </br>
sudo apt-get install wiringPi</br>
sudo apt-get install build-essential </br>
sudo apt-get install alsa-utils </br>
sudo apt-get install espeak </br>
sudo apt-get install libasound2-dev </br>
sudo apt-get install libbluetooth-dev </br>
sudo apt-get install bluetooth blueman bluez python-gobject python-gobject-2 </br>
sudo apt-get install -y libusb-dev libdbus-1-dev libglib2.0-dev libudev-dev libical-dev libreadline-dev </br>
sudo apt-get install i2c-tools </br>
sudo apt-get install qtconnectivity5-dev </br>
</br>
<b>Enable the I2C protocol feature in raspberry pi:</b></br>
sudo raspi-config</br>
Enable the I2C</br>
Reboot your system</br>
</br>
Building code: </br>
cd BalanceRobotPi </br>
qmake, make </br>
</br>
<p align="center"><a href="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote.jpg">
		<img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote.jpg" 
		name="remote" width="480" height="800" align="bottom" border="1"></a></p>
		
