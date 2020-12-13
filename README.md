# BalanceRobot & Remote control with Bluetooth BLE GATT Services & Google Speech to Text with Alsa sound cards.
This is a Balance Robot Project developed with QT 5</br>

The remote control has ios and qt version which can compile on all platforms that supports qt.</br>
https://www.youtube.com/watch?v=immSrXEHzQE&feature=youtu.be</br>
</br>
I use Rpi Motor Drive which works perfect with all raspberry versions.</br>
Please check below link for details.</br>
https://www.waveshare.com/wiki/RPi_Motor_Driver_Board</br>
</br>
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
I assume that your code and exec (bin) will be in /home/pi/BalanceRobotPI folder.

create a script start_robot.sh on your home folder (home/pi)
nano start_robot.sh<br>
place below code in it.

#!/bin/bash</br>
sudo chown root.root /home/pi/BalanceRobotPI/BalanceRobotPI</br>
sudo chmod 4755 /home/pi/BalanceRobotPI/BalanceRobotPI</br>
cd /home/pi/BalanceRobotPI</br>
sudo ./BalanceRobotPI</br>

make executable</br>
chmod +x start_robot.sh

Open a sample unit file using the command as shown below:</br>
sudo nano /lib/systemd/system/balancerobot.service</br>

Add in the following text :</br>

[Unit]</br>
Description=Balance Robot Service</br>
After=multi-user.target</br>
</br>
[Service]</br>
Type=idle</br>
ExecStart=/home/pi/start_robot.sh</br>
</br>
[Install]</br>
WantedBy=multi-user.target</br>
</br>
You should save and exit the nano editor.</br>

The permission on the unit file needs to be set to 644 :</br>
sudo chmod 644 /lib/systemd/system/balancerobot.service</br>

<b>Configure Systemd</b></br>
</br>
Now the unit file has been defined we can tell systemd to start it during the boot sequence :</br>
sudo systemctl daemon-reload</br>
sudo systemctl enable balancerobot.service</br>
Reboot the Pi and your custom service should run:</br>
sudo reboot</br>
check your service</br>
sudo systemctl status balancerobot.service</br>
turn switch off and on if service not start.</br>

<b>Required libraries: </b></br>
sudo apt update && sudo apt upgrade </br>
sudo apt install alsa-utils espeak libasound2-dev libbluetooth-dev libflac-dev bluetooth blueman bluez python-gobject python-gobject-2 libusb-dev libdbus-1-dev libglib2.0-dev libudev-dev libical-dev libreadline-dev i2c-tools libi2c-dev qt5-default qtconnectivity5-dev qtmultimedia5-dev libqt5multimedia5-plugins</br>
</br>
<b>Enable the I2C protocol feature in raspberry pi:</b></br>
sudo raspi-config</br>
Enable the I2C, SPI, Remote GPIO</br>
Reboot your system</br>
</br>
<b>Compile code: </b></br>
cd BalanceRobotPI </br>
qmake, make </br>
</br>
<b>Install latest wiringpi for rasp 4</b></br>
sudo apt purge wiringpi</br>
cd /tmp</br>
wget https://project-downloads.drogon.net/wiringpi-latest.deb</br>
sudo dpkg -i wiringpi-latest.deb</br>
gpio -v</br>
</br>
<p align="center"><a href="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote_ios.jpg">
		<img src="https://github.com/takyonxxx/BalanceRobotQT-Raspberry/blob/master/remote_ios.jpg" 
		name="remote" width="480" height="800" align="bottom" border="1"></a></p>
		
