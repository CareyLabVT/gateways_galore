# FLARE LoRaWAN sensor network
Under construction!

## What is [FLARE](https://flare-forecast.org/)?
> The FLARE project creates open-source software for flexible, scalable, robust, and near-real time iterative ecological forecasts in lakes and reservoirs. FLARE is composed of water temperature and meteorology sensors that wirelessly stream data, a data assimilation algorithm that uses sensor observations to update predictions from a hydrodynamic model and calibrate model parameters, and an ensemble-based forecasting algorithm to generate forecasts that include uncertainty… The FLARE open-source platform integrates edge and cloud computing open-source software frameworks: Docker containers for microservice deployment, Apache OpenWhisk for orchestration of event-driven actions, Git and EdgeVPN.io for edge-to-cloud transfers and remote management, and S3 for cloud staging and storage. The numerical forecasting core uses the General Lake Model and data assimilation based on the ensemble Kalman filter (EnKF).

## FLARE Sensor Network
The sensor networks currently used by the FLARE forecasting project are deployed at three freshwater drinking reservoirs located around Roanoke, VA. Two of the reservoirs, Beaverdam Reservoir and Carvins Cove, have a single gateway deployed on-site to achieve the goal of “edge-to-cloud transfers and remote management”. The Carvins Cove gateway is connected to grid power. The Beaverdam Reservoir gateway uses solar power. The network at the third reservoir, Falling Creek Reservoir, consists of three gateways. Two of the gateways are LTE-connected and run on grid power. The third gateway does not use an LTE-modem, but rather streams data over a LoRa-radio to one of the LTE-connected gateways, which forwards packets to the Internet. The LoRa-connected gateway uses solar power. 

![Current Network](/images/current_network.png)

#### Hardware
Each gateway is a Compulab Fitlet 2 or 3. Fitlet computers are designed for industrial and field applications and are very capable machines. Each gateway is connected to a Campbell Scientific datalogger via an Ethernet link. The datalogger is responsible for collecting measurements from each sensor and compiling it into a tidy CSV file. There are a few different datalogger models deployed at our field-sites, such as the CR6 and the CR3000. 

![FCR Met Station Internals](/images/met_station_internals.jpg)
[images of Fitlet and dataloggers]

#### Software
Each datalogger runs a CRBasic program that collects data from attached sensors at a specified interval. It stores this data into a DataTable. The gateway uses File Transfer Protocol (FTP) to retrieve the latest DataTable in the form of a CSV file. This CSV file is stored in the /data/ directory on the gateway. At times specified in the crontab file, the gateway will attempt to push the latest changes of the data CSV file to GitHub. For most of the gateways, this occurs directly over the LTE network interface, but the LoRa-connected gateway uses tncattach to establish a network interface over the radio link. The LoRa-connected gateway will then use the LoRa- and LTE-connected gateway as a Network Address Translation (NAT) device to push its updated CSV file to GitHub.

[more information about node maintenance]

### Challenges

#### Reliability

#### Power Consumption

#### Simplicity

#### Scalability

## Proposed Solution
[overview here - how does this address the aforementioned challenges?]

### Datalogger & Node

#### Hardware
There is an [RP2040 microcontroller (MCU)](https://www.adafruit.com/product/5714) connected to each datalogger in the network over a simple two-wire serial interface. The MCU is powered by grid power when available, but can be powered by the same 12V battery as the datalogger using a 12V to 5V step-down converter. The RP2040 MCU we chose has a built-in Semtech SX1276-based LoRa radio and 8MB of FLASH. This MCU acts as a single node in the network.
[picture of this configuration]

#### Software 
For the prototype, each node’s program is structured as three broad steps. 

The first step requests the metadata of the datalogger’s DataTable. This is to verify that the table is still the same one from the last session. The node compares the received metadata with the metadata stored in FLASH. 

The second step requests the most recent data from the datalogger. Currently, this uses an individual-acknowledgement protocol for each row of data: the node requests Row I, the datalogger returns Row I, the node requests Row I+1, the datalogger returns Row I+1, etc. until the datalogger is out of rows to send (it is up-to-date). At this point, the datalogger returns a “Done” message. As each row is received, it is checked for correctness and stored in FLASH memory if so.

The third step transfers the most recent CSV data to the gateway. [more information about that here]

### Gateway

#### Hardware
In the current network, each gateway is referred to as such because the computer acts as a gateway between the datalogger and the upstream cyberinfrastructure, like GitHub. However, a more standard use of the term gateway is a network device that bridges two networks. This is the purpose of the gateway in this proposed network. The gateway hardware consists of a Raspberry Pi 5 connected to a RAK5146 LoRaWAN HAT.

#### Software

### Test Deployment
[describe the current test deployment with photos of gateway setup and test node setup]

### Proposed FCR "Parallel Deployment"
[map and schematic of FCR parallel deployment]

### Problems and Future Work


## Setup
[link to separate setup pages]

### Microcontroller


### Datalogger

### Gateway

