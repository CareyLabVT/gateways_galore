# Gateway Software Setup
## Hardware Needed
- [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/)
- [RAK5146 PiHAT Kit for LoRaWAN](https://store.rakwireless.com/products/rak5146-kit?utm_medium=header&utm_source=rak2245-pi-hat&variant=41577988161734) (should come with an antenna and GPS module)
- [27W+ PD-compatible USB-C Power Supply](https://www.raspberrypi.com/products/27w-power-supply/)
- 16GB+ microSD card
- Small Phillips screwdriver
- Ethernet cable (and USB-Ethernet adapter if your computer lacks an Ethernet port)

## Hardware Setup
1. Insert the LoRa Concentrator Module into the slot on the PiHAT at roughly a 45 degree angle. Gently press it flat towards the board and then screw the two mounting screws in. 
    
    **Note:** If you notice resistance, **don't force it!**. Try reseating the Concentrator Module into the slot and trying again.
2. Attach the GPS antenna (box-shaped) to the labeled GPS uFL connector. Attach the LoRa antenna (flat rectangle) to the labeled LoRa uFL connector.
3. Insert 4 of the brass spacers into the Raspberry Pi through the bottom. Screw the other 4 brass spacers into the first 4 spacers. 
4. Gently, but firmly, press the PiHAT onto the 40-pin connector of the Raspberry Pi 5 such that the PiHAT should fit within the form factor of the Raspberry Pi. It shouldn't look like an airplane wing off the side.

## Raspberry Pi OS Setup
1. Install [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
2. Insert the microSD card into your machine (not the Raspberry Pi 5).
3. In Raspberry Pi Imager, select the Raspberry Pi 5 device.
4. Under Operating System, select Raspberry Pi OS (other). Select Raspberry Pi OS Lite (64-bit).
5. Under Storage, select your microSD card. 
   
    **Note:** Be careful! You can easily overwrite the wrong drive here and erase everything!
6. When prompted “Would you like to apply OS customisation settings?”, select Edit settings.
    1. Check Set hostname and set it to “raspberry”.
    2. Check Set username and password and set the username to “pi”. Choose a good password.
    3. Save the settings.
7. Select Next and you should receive a warning about erasing everything on the drive you selected. This is your final chance to make sure it is the correct drive.
8. Once it has completed, remove the microSD and insert it into the Raspberry Pi 5.
9. Connect the Raspberry Pi 5 to the power adapter.

## Raspberry Pi Setup
1. Direct connect your machine to the Raspberry Pi over Ethernet.
2. In a terminal, SSH into the Raspberry Pi using the username and hostname specified in the Raspberry Pi Imager. It should look like
    ```bash
    ssh pi@raspberry # or pi@raspberry.local
    ```
2. Sign in using the username and password you previously set in the Raspberry Pi Imager.
3. Set a static IP address on the Raspberry Pi so you do not lose your SSH session.


## Connect to eduroam WiFi
https://vtluug.org/wiki/Virginia_Tech_Wifi#Obtaining_the_Certificate_Chain

You need the following three certificates:


### Obtaining eduroam certificate:
This should be done on the computer you are using to SSH into the RAK gateway.
1. On [Virginia Tech's Certificate Manager](https://certs.it.vt.edu/search#), search for `eduroam.nis.vt.edu`. 
2. Locate the most recent entry and click the **Action** dropdown. 
3. Download the *Server Certificate Chain Not Including Root (pem)*
4. In a terminal window, locate the folder that you downloaded the `eduroam.nis.vt.edu.pem` file to and type the following command to secure-copy the file to the RAK gateway.
    ```
    scp .\eduroam.nis.vt.edu.pem pi@rak-gateway:~
    ```
    Note: The gateway may have a different username or hostname than `pi` or `rak-gateway`, respectively. 
5. Enter the RAK gateway's password. It should be in the home directory of the RAK gateway now (which you can navigate to with `cd ~`).

### Obtaining InCommon RSA Server CA2 certificate
This should be done on the computer you are using to SSH into the RAK gateway.
1. Download the [InCommon RSA Server CA2 certificate](https://uit.stanford.edu/sites/default/files/2023/10/11/incommon-rsa-ca2.pem). This link may at some point become broken and you may need to find a new source for it. Reference the Virginia Tech Wiki page that is linked above for help.
2. scp this bad boy over too

### 

cat USERTrust_RSA_Certification_Authority.pem InCommonRSAServerCA_2.pem > ca.pem

openssl verify -verbose -purpose sslserver -CAfile ca.pem eduroam.nis.vt.edu.pem

openssl x509 -in file_of_cert_you_want_to_check -noout -subject (for all three certificates)

openssl x509 -in eduroam.nis.vt.edu.pem -outform der | sha256su

sudo nano /etc/NetworkManager/system-connections/eduroam.nmconnection
-> and paste the following into the file:
```
[connection]
id=eduroam
type=wifi
autoconnect=true

[wifi]
ssid=eduroam
mode=infrastructure

[wifi-security]
key-mgmt=wpa-eap

[802-1x]
eap=peap;
identity=<your_network_username>
anonymous-identity=anonymous@vt.edu
password=<your_network_password>
phase2-auth=mschapv2
# Use the VT-recommended root CA and pin the server's domain suffix:
ca-cert=file:///etc/ssl/certs/USERTrust_RSA_Certification_Authority.pem
domain-suffix-match=nis.vt.edu

[ipv4]
method=auto

[ipv6]
method=auto
```

sudo chown root:root /etc/NetworkManager/system-connections/eduroam.nmconnection

sudo chmod 600 /etc/NetworkManager/system-connections/eduroam.nmconnection

sudo nmcli connection reload

sudo nmcli connection up eduroam

run a test ping, you may have to specify the interface as such:
```
ping -I wlan0 8.8.8.8
```

## Set up Wireguard
[repeat steps outlined in SPIN@VT notion page]
