
# Complie
```sh
make
```

# Use
```sh
sudo cp screenblank /usr/sbin/

sudo cp service/screenblank.service /lib/systemd/system/
sudo systemctl enable screenblank.service
#disbale raspberry pi official screen blanking
sudo raspi-config nonint do_blanking 1
```

# Reboot
```sh
sudo reboot
```

# Modify default screen blank time

The default screen blank time is 30 seconds,and the time can be modified by modifying the parameters passed by the screenblank.service service. 
The minimum screen blank time is 5 seconds. If the configuration parameter is less than 5 seconds, it will be processed as 5 seconds.
```sh
sudo nano /lib/systemd/system/screenblank.service
```
Modify the parameter after `ExecutStart=screenblank`, which represents seconds.
