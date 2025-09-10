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
**NOTE:** I'm writing the bulk of this documentation in retrospect, so there may be some missing packages. When calling some commands or running some programs, it may return an error that a certain package is not installed. To remedy this, just use 
```bash
sudo apt install <name of missing package>
```
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

### [Set a static IP address for direct connection Ethernet](https://www.abelectronics.co.uk/kb/article/31/set-a-static-ip-address-on-raspberry-pi-os-bookworm)
You should do this to avoid losing SSH connection mid-session. 

1. Find the name of the network interface to set as static. 
    ```bash
    sudo nmcli -p connection show
    ```
    This may look something like `eth0` or `Wired connection 1`.
2. Update the network connection and set the new IP address, gateway, and DNS.
    ```bash
    sudo nmcli c mod "Wired connection 1" ipv4.addresses 10.0.0.220/24 ipv4.method manual

    sudo nmcli con mod "Wired connection 1" ipv4.gateway 10.0.0.1

    sudo nmcli con mod "Wired connection 1" ipv4.dns 10.0.0.1
    ```
3. Restart the network connection
    ```bash
    sudo nmcli c down "Wired connection 1" && sudo nmcli c up "Wired connection 1"
    ```

### [Connect to eduroam WiFi](https://vtluug.org/wiki/Virginia_Tech_Wifi#Obtaining_the_Certificate_Chain)

You can skip this step for now if you can confidently connect your Raspberry Pi to both Internet and your machine over Ethernet, but it will be needed if deploying remotely on campus.

You need the following three certificates:
- USERTrust RSA Certification Authority
- InCommon RSA Server CA2
- eduroam.nis.vt.edu

1. Obtain the USERTrust RSA Certification Authority
    This file is likely located at `/etc/ssl/certs/USERTrust_RSA_Certification_Authority.pem`. That's where I found it at least on Debian Bookworm on the Raspberry Pi 5.

2. Obtain the InCommon RSA Server CA2
    The VT LUUG guide linked above provides a link to this certificate, but the download is not a `.pem` file. So I found another source for the file [here](https://uit.stanford.edu/sites/default/files/2023/10/11/incommon-rsa-ca2.pem). Download this file to your machine.

3. Obtain the eduroam.nis.vt.edu certificate
    1. On your machine, visit [Virginia Tech's Certificate Manager](https://certs.it.vt.edu/search#). You may need to sign in with your VT account.
    2. Search for `eduroam.nis.vt.edu`.
    3. Locate the most recent entry and click the **Action** dropdown. 
    4. Download the *Server Certificate Chain Not Including Root (pem)*

4. In a new terminal window on your machine, locate the folder that you downloaded the `eduroam.nis.vt.edu.pem` and `incommon-rsa-ca2.pem` file to and type the following command to secure-copy the file to the RAK gateway.
    ```
    scp .\eduroam.nis.vt.edu.pem .\incommon-rsa-ca2.pem pi@rak-gateway:~
    ```
    **Note:** The gateway may have a different username or hostname than `pi` or `rak-gateway`, respectively. 

5. Enter the Raspberry Pi's password when prompted. The certificates should be in the home directory of the RAK gateway now (which you can navigate to with `cd ~`).

6. Validate the certificates. 
    1. Concatenate the non-leaf certificates in to a single file.
    ```bash
    cat /etc/ssl/certs/USERTrust_RSA_Certification_Authority.pem incommon-rsa-ca2.pem > ca.pem
    ```
    2. Verify the certificates are signed correctly.
    ```bash
    openssl verify -verbose -purpose sslserver -CAfile ca.pem eduroam.nis.vt.edu.pem
    ```
    3. Verify the subject of the root and leaf certificates.
    ```bash
    openssl x509 -in file_of_cert_you_want_to_check -noout -subject
    ```

    4. Validate the eduroam certificate.
    ```bash
    openssl x509 -in eduroam.nis.vt.edu.crt -outform der | sha256sum
    ```

    5. Create the NetworkManager configuration file for eduroam.
        1. Create the configuration file.
        ```bash
        sudo nano /etc/NetworkManager/system-connections/eduroam.nmconnection
        ```

        2. Copy and paste the following configuration into the file. Make sure to change `identity` and `password`.
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

        3. Now lock down the file permissions.
        ```bash
        sudo chown root:root /etc/NetworkManager/system-connections/eduroam.nmconnection
        sudo chmod 600 /etc/NetworkManager/system-connections/eduroam.nmconnection
        ```

        4. Reload the connection and do a test ping Google's DNS server as a connection test.
        ```bash
        sudo nmcli connection reload
        sudo nmcli connection up eduroam

        ping -I wlan0 8.8.8.8
        ```

### Set up Wireguard VPN
This section is necessary for remote maintenance.
[repeat steps outlined in SPIN@VT notion page]

### Configure Raspberry Pi as gateway
#### Basic Raspberry Pi settings
1. Change the Raspberry Pi settings:

    ```bash
    sudo raspi-config
    ```
    
    1. Now navigate into the `3 Interface Options` and enable the following interfaces:
        1. SSH
        2. SPI
        3. I2C
        4. Serial (but do not allow login over Serial)

    2. Navigate into the `5 Localisation Options`.
        1. Set the locale to `en_US.UTF-8`
        2. Set the WLAN country to `United States`

2. Update and upgrade packages, libraries, etc.:
    ```bash
    sudo apt update && sudo apt upgrade -y
    ```

#### Set up RAK Wireless Gateway software
1. Install Git.
    ```bash
    sudo apt install git -y
    ```
2. Clone the `rak_common_for_gateway` repository.
    ```bash
    git clone https://github.com/RAKWireless/rak_common_for_gateway.git ~/rak_common_for_gateway
    ```

3. Navigate into the repository.
    ```bash
    cd ~/rak_common_for_gateway
    ```

4. Now modify the installation script so that it does not install Chirpstack. Otherwise it will install Chirpstack v3 and nothing will work as expected down the road.

    1. ```bash
        sudo nano ./install.sh
        ```
    2. Under the following block of code, insert the line `INSTALL_CHIRPSTACK=0` and then CTRL+X to exit nano.
        ```
        while true; do
            case "${1}" in
                --help)
                shift;
                print_help
                ;;

                --img)
                shift;
                CREATE_IMG="create_img"
                ;;

                --chirpstack)
                shift;
                if [[ -n "${1}" ]]; then
                    if [ "not_install" = "${1}" ]; then
                        INSTALL_CHIRPSTACK=0
                    elif [ "install" = "${1}" ]; then
                        INSTALL_CHIRPSTACK=1
                    else
                        echo "invalid value"
                        exit
                    fi

                    if [ $rpi_model -ne 3 ] && [ $rpi_model -ne 4 ]; then
                        INSTALL_CHIRPSTACK=1
                    fi
                    shift;
                fi
                ;;

                --)
                shift;
                break;
                ;;
            esac
        done
        ```
    3. Now run the installation script.
        ```bash
        sudo ./install.sh
        ```
    4. Reboot the Raspberry Pi
        ```bash
        sudo reboot
        ```
    5. Log back into the Raspberry Pi. 
    
        **NOTE:** [The RAK5146 Quickstart page](https://docs.rakwireless.com/product-categories/wislink/rak5146/quickstart/) provides some more information that may help resolve connection issues that arise after installing the RAK Wireless Gateway software. 
    6. Now edit the gateway configuration.
        ```bash
        sudo gateway-config
        ```
        
        1. If at this point you have not changed the password to the Raspberry Pi, now is a good time. Select `1 Set pi password` to do so.

        2. Select `2 Setup RAK Gateway Channel Plan` and then select `Server is other server`. Select `1 Server Channel-plan configuration` and select `US_902_928`. Now set the server IP to `127.0.0.1` if it is not already.

        3. In the main `gateway-config` menu, select `4 Edit packet-forwarder config`. Replace whatever is in the file with the following:
            ```json
            {
                "SX130x_conf": {
                    "com_type": "SPI",
                    "com_path": "/dev/spidev0.0",
                    "lorawan_public": true,
                    "clksrc": 0,
                    "antenna_gain": 0, /* antenna gain, in dBi */
                    "full_duplex": false,
                    "fine_timestamp": {
                        "enable": false,
                        "mode": "all_sf" /* high_capacity or all_sf */
                    },

                    "radio_0": {
                        "enable": true,
                        "type": "SX1250",
                        "freq": 904300000,
                        "rssi_offset": -215.4,
                        "rssi_tcomp": {"coeff_a": 0, "coeff_b": 0, "coeff_c": 20.41, "coeff_d": 2162.56, "coeff_e": 0},
                        "tx_enable": true,
                        "tx_freq_min": 923000000,
                        "tx_freq_max": 928000000,
                        "tx_gain_lut":[
                            {"rf_power": 12, "pa_gain": 1, "pwr_idx": 6},
                            {"rf_power": 13, "pa_gain": 1, "pwr_idx": 7},
                            {"rf_power": 14, "pa_gain": 1, "pwr_idx": 8},
                            {"rf_power": 15, "pa_gain": 1, "pwr_idx": 9},
                            {"rf_power": 16, "pa_gain": 1, "pwr_idx": 10},
                            {"rf_power": 17, "pa_gain": 1, "pwr_idx": 11},
                            {"rf_power": 18, "pa_gain": 1, "pwr_idx": 12},
                            {"rf_power": 19, "pa_gain": 1, "pwr_idx": 13},
                            {"rf_power": 20, "pa_gain": 1, "pwr_idx": 14},
                            {"rf_power": 21, "pa_gain": 1, "pwr_idx": 15},
                            {"rf_power": 22, "pa_gain": 1, "pwr_idx": 16},
                            {"rf_power": 23, "pa_gain": 1, "pwr_idx": 17},
                            {"rf_power": 24, "pa_gain": 1, "pwr_idx": 18},
                            {"rf_power": 25, "pa_gain": 1, "pwr_idx": 19},
                            {"rf_power": 26, "pa_gain": 1, "pwr_idx": 21},
                            {"rf_power": 27, "pa_gain": 1, "pwr_idx": 22}
                        ]
                    },
                    "radio_1": {
                        "enable": true,
                        "type": "SX1250",
                        "freq": 905000000,
                        "rssi_offset": -215.4,
                        "rssi_tcomp": {"coeff_a": 0, "coeff_b": 0, "coeff_c": 20.41, "coeff_d": 2162.56, "coeff_e": 0},
                        "tx_enable": false
                    },
                    "chan_multiSF_All": {"spreading_factor_enable": [ 5, 6, 7, 8, 9, 10, 11, 12 ]},
                    "chan_multiSF_0": {"enable": true, "radio": 0, "if": -400000},  /* Freq : 903.9 MHz*/
                    "chan_multiSF_1": {"enable": true, "radio": 0, "if": -200000},  /* Freq : 904.1 MHz*/
                    "chan_multiSF_2": {"enable": true, "radio": 0, "if":  0},       /* Freq : 904.3 MHz*/
                    "chan_multiSF_3": {"enable": true, "radio": 0, "if":  200000},  /* Freq : 904.5 MHz*/
                    "chan_multiSF_4": {"enable": true, "radio": 1, "if": -300000},  /* Freq : 904.7 MHz*/
                    "chan_multiSF_5": {"enable": true, "radio": 1, "if": -100000},  /* Freq : 904.9 MHz*/
                    "chan_multiSF_6": {"enable": true, "radio": 1, "if":  100000},  /* Freq : 905.1 MHz*/
                    "chan_multiSF_7": {"enable": true, "radio": 1, "if":  300000},  /* Freq : 905.3 MHz*/
                    "chan_Lora_std":  {"enable": true, "radio": 0, "if":  300000, "bandwidth": 500000, "spread_factor": 8,                                     /* Freq : 904.6 MHz*/
                                    "implicit_hdr": false, "implicit_payload_length": 17, "implicit_crc_en": false, "implicit_coderate": 1},
                    "chan_FSK":       {"enable": false, "radio": 1, "if":  300000, "bandwidth": 125000, "datarate": 50000}                                     /* Freq : 868.8 MHz*/
                },

                "gateway_conf": {
                    "gateway_ID": "AA555A0000000000",
                    /* change with default server address/ports */
                    "server_address": "127.0.0.1",
                    "serv_port_up": 1700,
                    "serv_port_down": 1700,
                    /* adjust the following parameters for your network */
                    "keepalive_interval": 10,
                    "stat_interval": 30,
                    "push_timeout_ms": 100,
                    /* forward only valid packets */
                    "forward_crc_valid": true,
                    "forward_crc_error": false,
                    "forward_crc_disabled": false,
                    /* GPS configuration */
                    "gps_tty_path": "/dev/ttyAMA0",
                    /* GPS reference coordinates */
                    "ref_latitude": 0.0,
                    "ref_longitude": 0.0,
                    "ref_altitude": 0,
                    /* Beaconing parameters */
                    "beacon_period": 0,     /* disable class B beacon, set to 128 enable beacon */
                    "beacon_freq_hz": 923300000,
                    "beacon_freq_nb": 8,
                    "beacon_freq_step": 600000,
                    "beacon_datarate": 12,
                    "beacon_bw_hz": 500000,
                    "beacon_power": 27
                },

                "debug_conf": {
                    "ref_payload":[
                        {"id": "0xCAFE1234"},
                        {"id": "0xCAFE2345"}
                    ],
                    "log_file": "loragw_hal.log"
                }
            }
            ```

        4. Save and exit from the config file. Once again in the main `gateway-config` menu, select `5 Configure WiFi`. Select `2 Enable Client Mode/Disable AP Mode`. Quit from this menu.

        5. Select `3 Restart packet-forwarder`. Quit from `gateway-config`.
            **NOTE:** You may have to restart the Network Manager to reconnect to eduroam. It's always helpful to run a test ping after reconnecting to WiFi to ensure you have Internet access.

        6. Just as a sanity check, run the following command to ensure there is no Chirpstack software installed by RAK Wireless.
            ```bash
            sudo apt remove chirpstack-*
            ```

#### Set up Chirpstack
The following steps come from the [Chirpstack Quickstart Debian / Ubuntu page](https://www.chirpstack.io/docs/getting-started/debian-ubuntu.html).
1. Install the dependencies for Chirpstack
    ```bash	
    sudo apt install \
    mosquitto \
    mosquitto-clients \
    redis-server \
    redis-tools \
    Postgresql
    ```

2. Configure PostgreSQL. 
    
    1. Open the PostgreSQL prompt. Steps 2-6 should be executed within the PostgreSQL prompt.
        ```bash
        sudo -u postgres psql
        ```
    
    2. Create a role for authentication.
        ```postgresql
        create role chirpstack with login password 'chirpstack';
        ```

    3. Create a database.
        ```postgresql
        create database chirpstack with owner chirpstack;
        ```
    
    4. Change to Chirpstack database.
        ```postgresql
        \c chirpstack
        ```
    
    5. Create `pg_trgm` extension.
        ```postgresql
        create extension pg_trgm;
        ```

    6. Exit the PostgreSQL prompt.
        ```postgresql
        \q
        ```

3. Set up Chirpstack software repository.

    1. Install `gpg`
        ```bash
        sudo apt install gpg
        ```

    2. Set up the key for the Chirpstack repository.
        ```bash
        sudo mkdir -p /etc/apt/keyrings/

        sudo sh -c 'wget -q -O - https://artifacts.chirpstack.io/packages/chirpstack.key | gpg --dearmor > /etc/apt/keyrings/chirpstack.gpg'
        ```
    
    3. Add the repository to the repository list.
        ```bash
        echo "deb [arch=arm64 signed-by=/etc/apt/keyrings/chirpstack.gpg] https://artifacts.chirpstack.io/packages/4.x/deb stable main" | sudo tee /etc/apt/sources.list.d/chirpstack.list
        ```

    4. Update the `apt` package cache.
        ```bash
        sudo apt update
        ```

4. Set up `chirpstack-gateway-bridge`.
    
    1. Install the package using apt.
        ```bash
        sudo apt install chirpstack-gateway-bridge
        ```

    2. Update the Gateway Bridge configuration file to match the US region prefix.
        
        1. Open the configuration file in nano.
            ```bash
            sudo nano /etc/chirpstack-gateway-bridge/chirpstack-gateway-bridge.toml
            ``` 
        
        2. Locate the `[integration.mqtt] section` and edit to to look like:
            ```toml
            [integration.mqtt]
            event_topic_template="us915_0/gateway/{{ .GatewayID }}/event/{{ .EventType }}"
            command_topic_template="us915_0/gateway/{{ .GatewayID }}/command/#"
            ```

    3. Start the Gateway Bridge.
        ```bash
        sudo systemctl start chirpstack-gateway-bridge
        ```

    4. Enable the Gateway Bridge.
        ```bash
        sudo systemctl enable chirpstack-gateway-bridge
        ```

5. Configure Chirpstack.

    1. Install Chirpstack.
        ```bash
        sudo apt install chirpstack
        ```

    2. Open the global Chirpstack configuration file.
        ```bash
        sudo nano /etc/chirpstack/chirpstack.toml
        ```
    
    3. Replace the text in the configuration file with the following:
        ```toml
        # Logging.
        [logging]

        # Log level.
        #
        # Options are: trace, debug, info, warn error.
        level = "info"


        # PostgreSQL configuration.
        [postgresql]

        # PostgreSQL DSN.
        #
        # Format example: postgres://<USERNAME>:<PASSWORD>@<HOSTNAME>/<DATABASE>?sslmode=<SSLMODE>.
        #
        # SSL mode options:
        #  * disable - no SSL
        #  * require - Always SSL (skip verification)
        #  * verify-ca - Always SSL (verify that the certificate presented by the server was signed by a trusted CA)
        #  * verify-full - Always SSL (verify that the certification presented by the server was signed by a trusted CA and the server host name matches the one in the certificate)
        dsn = "postgres://chirpstack:chirpstack@localhost/chirpstack?sslmode=disable"

        # Max open connections.
        #
        # This sets the max. number of open connections that are allowed in the
        # PostgreSQL connection pool.
        max_open_connections = 10

        # Min idle connections.
        #
        # This sets the min. number of idle connections in the PostgreSQL connection
        # pool (0 = equal to max_open_connections).
        min_idle_connections = 0


        # Redis configuration.
        [redis]

        # Server address or addresses.
        #
        # Set multiple addresses when connecting to a cluster.
        servers = ["redis://localhost/"]

        # Redis Cluster.
        #
        # Set this to true when the provided URLs are pointing to a Redis Cluster
        # instance.
        cluster = false


        # Network related configuration.
        [network]

        # Network identifier (NetID, 3 bytes) encoded as HEX (e.g. 010203).
        net_id = "000000"

        # Enabled regions.
        #
        # Multiple regions can be enabled simultaneously. Each region must match
        # the 'name' parameter of the region configuration in '[[regions]]'.
        enabled_regions = [
            #"as923",
            #"as923_2",
            #"as923_3",
            #"as923_4",
            #"au915_0",
            #"cn470_10",
            #"cn779",
            #"eu433",
            #"eu868",
            #"in865",
            #"ism2400",
            #"kr920",
            #"ru864",
            "us915_0",
            "us915_1",
        ]


        # API interface configuration.
        [api]
        enabled=true
        # interface:port to bind the API interface to.
        bind = "0.0.0.0:8080"

        # Secret.
        #
        # This secret is used for generating login and API tokens, make sure this
        # is never exposed. Changing this secret will invalidate all login and API
        # tokens. The following command can be used to generate a random secret:
        #   openssl rand -base64 32
        secret = "you-must-replace-this"


        [integration]
        enabled = ["mqtt"]

        [integration.mqtt]
            server = "tcp://localhost:1883/"
            json = true

        [integration.mqtt.client]
                client_cert_lifetime="12months"
                ca_cert="/etc/chirpstack/certs/ca.pem"
                ca_key="/etc/chirpstack/certs/ca-key.pem"

        [gateway]
                client_cert_lifetime="12months"
                ca_cert="/etc/chirpstack/certs/ca.pem"
                ca_key="/etc/chirpstack/certs/ca-key.pem"
        ```
    
6. Configure MQTT.

    This information is pulled from the [Chirpstack Mosquitto TLS configuration guide](https://www.chirpstack.io/docs/guides/mosquitto-tls-configuration.html).

    1. Install the `cfssl` utility.
        ```bash
        sudo apt-get install golang-cfssl
        ```

    2. Generate CA certificate and key.

        1. Create `ca-csr.json` using:
            ```bash
            sudo nano cs-csr.json
            ```
        2. Copy the following into the file. Save and exit.
            ```json
            {
                "CN": "ChirpStack CA",
                "key": {
                    "algo": "rsa",
                    "size": 4096
                }
            }
            ```

        3. Similarly, create `ca-config.json` and copy the following into the file. Save and exit.
            ```json
            {
                "signing": {
                    "default": {
                    "expiry": "8760h"
                    },
                    "profiles": {
                        "server": {
                            "expiry": "8760h",
                            "usages": [
                            "signing",
                            "key encipherment",
                            "server auth"
                            ]
                        }
                    }
                }
            }
            ```

        4. Generate the CA certificate and key.
            ```bash
            cfssl gencert -initca ca-csr.json | cfssljson -bare ca
            ```

    3. Generate MQTT server-certificate.

        1. Create `mqtt-server.json` and copy the following into the file. You can change `example.com` to "the hostname that will be used by clients that will connect to the MQTT broker", but I did not change it and I have not had any problems. Despite the information on the website on how this works, I frankly do not understand how it works. Do what you will with that information.
            ```json
            {
                "CN": "example.com",
                "hosts": [
                    "example.com"
                ],
                "key": {
                    "algo": "rsa",
                    "size": 4096
                }
            }
            ```

        2. Generate the MQTT server-certificate.
            ```bash
            cfssl gencert -ca ca.pem -ca-key ca-key.pem -config ca-config.json -profile server mqtt-server.json | cfssljson -bare mqtt-server
            ```

    4. Configure Chirpstack for the MQTT certificates.
        
        1. Create a new `certs` directory and copy the certificate and key into it.
            ```bash
            mkdir -p /etc/chirpstack/certs

            cp ca.pem /etc/chirpstack/certs

            cp ca-key.pem /etc/chirpstack/certs
            ```

        2. Update the ownership and permissions of the created directory and files.
            ```bash
            sudo chown root:chirpstack /etc/chirpstack/certs/ca-key.pem

            sudo chmod 640 /etc/chirpstack/certs/ca-key.pem

            sudo chown root:chirpstack /etc/chirpstack/certs
            
            sudo chmod 750 /etc/chirpstack /etc/chirpstack/certs
            ```
        
        3. Verify from the service account that the key and certificate are readable.
            ```bash
            sudo -u chirpstack test -r /etc/chirpstack/certs/ca-key.pem && echo "readable" || echo "not readable"
            ```

        4. Restart Chirpstack
            ```bash
            sudo systemctl restart chirpstack
            ```

    5. Configure Mosquitto for the MQTT certificates.

        1. Create a new `certs` directory and copy the certificates and key into it.
            ```bash
            mkdir -p /etc/mosquitto/certs

            cp ca.pem /etc/mosquitto/certs

            cp mqtt-server.pem /etc/mosquitto/certs

            cp mqtt-server-key.pem /etc/mosquitto/certs
            ```

        2. Update the ownership and permissions of the created directory and files.
            ```bash
            sudo chown root:chirpstack /etc/mosquitto/certs/mqtt-server-key.pem

            sudo chmod 640 /etc/mosquitto/certs/mqtt-server-key.pem

            sudo chown root:chirpstack /etc/mosquitto/certs
            
            sudo chmod 750 /etc/mosquitto /etc/mosquitto/certs
            ```

        3. Create an ACL file.
            ```bash
            sudo nano /etc/mosquitto/acl
            ```
            And copy the following into the file, save and exit:
            ```acl
            pattern readwrite +/gateway/%u/#
            pattern readwrite application/%u/#
            ```

        4. Restart Chirpstack again.

#### Configure Chirpstack web interface
Access the Chirpstack web interface using the IP of the Raspberry Pi and port number 8080 in a web browser.
```
http://<IP.of.RPi>:8080
```

1. Sign in using the default credentials. You need to change these.
    
    Username: `admin`

    Password: `admin`

2. Select **Network Server > Tenants** and select **Add tenant**.

    1. Create a new name for the tenant. I used something generic like `Chirpstack` for testing purposes.

    2. Select **Tenant can have gateways**

    3. Select **Submit** to save the new tenant.
     
    You should now see the new tenant under the **Network Server > Tenants** page.

3. Select **Tenant > Gateways** and select **Add gateway**.

    1. Create a new name for the gateway. I used the name `RAK5146 PiHAT`.

    2. Next enter the **Gateway ID (EUI64)** using the steps below.

        1. SSH into the Raspberry Pi.

        2. Run the command `sudo gateway-config`.

        3. The Gateway ID is located at the top of the menu. Copy it into the web interface. It is not case sensitive.

    3. You can leave the remaining setting at their default values.

    4. Select **Submit** to save the new gateway. 

    Wait a minute or so and refresh the **Dashboard**. The gateway should appear under the **Active Gateways** Pie chart as **Online**. If not, then the most likely cause is the MQTT broker. Refer to the Chirpstack documentation (and/or ChatGPT - it was somewhat helpful) for troubleshooting help.

4. Select **Tenant > Applications** and select **Add application**.

    1. Enter a descriptive name. 
    2. Select **Submit**.

At this point, you should have a fully operational gateway!

### Adding a device to the Chirpstack LoRaWAN network

If you have not already, please read the [documentation page on implementing a Chirpstack node](/node/node_setup.md) and revisit this page.

#### Create a new Device Profile

Select **Tenant > Device Profiles** and select **Add device profile**.

1. Enter a descriptive name for the profile name. I chose `Adafruit Feather RP2040` to reflect the microcontroller used for testing.

2. Set the **Region** to `US915`.

3. Set the **Region Configuration** to `US915 (channels 0-7 + 64)`.

4. Set the **MAC version** to `LoRaWAN 1.1.0` (assuming you are using the RadioLib library).

5. Set the **Regional parameters revision** to `A`.

6. Set the **ADR algorithm** to `Default ADR algorithm (LoRa only)`.

7. Select **Flush queue on activate** and leave **Allow roaming** deselected.

8. Leave the **Expected uplink interval (secs)** at `3600` for now. You may have to change this down the road.

9. Leave the **Device-status request frequency (req/day)** at `1` for now.

10. Leave the **RX1 Delay** at `0`.

#### Create a new Device

Select your application under **Tenant > Applications**. Select **Add device**.

1. Enter a descriptive name for the device name. In a field deployment, this may be something like the site ID.

2. For **Device EUI (EUI64)**, make sure `MSB` is selected and click the reload symbol to the right of the `MSB` dropdown. The text box should be populated with a 16-length hexadecimal string. Copy this somewhere to use later.

3. For **Join EUI (EUI64)**, populate this value with 16 0's:
    ```
    0000000000000000
    ```

4. Select the appropriate **Device Profile** you created earlier. 

5. Select **Submit**. 

You should now have been directed to the **OTAA Keys** tab of your newly-created device. 

6. In a similar fashion to the Device EUI, generate an **Application key** and copy this somewhere to use later. 

7. Again, generate a **Network key** and copy this somewhere to use later.

7. Select **Submit** once again.

Your device should now be configured in the Chirpstack network. Review the [documentation page on implementing a Chirpstack node](/node/node_setup.md) to see how to use the Device EUI, Application key, and Network key.